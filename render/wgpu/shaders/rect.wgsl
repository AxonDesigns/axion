// Copyright 2026 Axon Works
// SPDX-License-Identifier: Apache-2.0
//
// ax_render_wgpu: rects and rounded rects as instanced quads with analytic area coverage
// (decision 55). Embedded into the build by render/wgpu/CMakeLists.txt.
//
// Coverage is the exact area of the unit pixel square on the inner side of each edge, computed
// in device pixels from the edge's normal and signed distance:
//   - a straight edge's coverage is the CDF of the pixel square projected onto the edge normal;
//   - a pair of parallel edges (a rect's two x edges, or two y edges) is a slab, covered exactly
//     by cov(left) + cov(right) - 1, so rects thinner than a pixel get their true area;
//   - a rect is the product of its two slabs (exact when the slabs are axis-aligned on screen);
//   - a rounded corner uses the circle's distance, treated as locally straight; the result is
//     the smaller of that and the slabs' coverage.
// The distances need the pixel's footprint in the shape's local space: d(local)/d(device). For
// affine transforms it is constant per instance, so the CPU computes it from the inverse of the
// transform's linear part and passes it in `inv`: exact, and identical on every implementation.
// FLAG_FOOTPRINT_FROM_DERIVATIVES takes it from dpdxFine/dpdyFine instead, for transforms with
// perspective (not used yet).
//
// Clips (decision 93): rect and rounded-rect clips are records in a storage buffer, each with its
// device-to-local transform, so a clip is evaluated in its own layer's space with the same
// coverage as an item; an instance names the innermost record of its chain and each record its
// parent. The coverages multiply, innermost first, as in ax_render_soft (decision 81). Clips that
// are plain rects aligned with the device's axes are intersected on the CPU into one record in
// device space; a chain is at most MAX_CLIPS records long (the CPU approximates beyond).

const FLAG_ROUNDED: u32 = 1u;
const FLAG_FOOTPRINT_FROM_DERIVATIVES: u32 = 2u;
const MAX_CLIPS: u32 = 5u; // RW_MAX_CLIP_CHAIN complex clips and one device rect

struct Clip {
    m: vec4f,     // device pixel -> local, linear part: a, b, c, d (as `inv`)
    t: vec4f,     // translation tx, ty; z = flags; w = parent record + 1 (0: none)
    rect: vec4f,  // x, y, w, h in the clip's local units
    radii: vec4f, // clamped
};

@group(0) @binding(0) var<storage, read> clips: array<Clip>;

struct Instance {
    @location(0) rect: vec4f,   // x, y, w, h in local units
    @location(1) radii: vec4f,  // top-left, top-right, bottom-right, bottom-left (clamped)
    @location(2) color: vec4f,  // sRGB-encoded, premultiplied
    @location(3) xform: vec4f,  // local -> clip space, linear part: a, b, c, d
    @location(4) xlate: vec4f,  // local -> clip space, translation: tx, ty; z = flags;
                                // w = the innermost clip record + 1 (0: none)
    @location(5) inv: vec4f,    // device pixel -> local: (ia, ib) = d(local)/dX, (ic, id) = d/dY
};

struct VOut {
    @builtin(position) pos: vec4f,
    @location(0) local: vec2f,
    @location(1) @interpolate(flat) rect: vec4f,
    @location(2) @interpolate(flat) radii: vec4f,
    @location(3) @interpolate(flat) color: vec4f,
    @location(4) @interpolate(flat) inv: vec4f,
    @location(5) @interpolate(flat) flags: u32,
    @location(6) @interpolate(flat) clip: u32,
};

@vertex
fn vs(@builtin(vertex_index) vi: u32, inst: Instance) -> VOut {
    // Triangle strip: (0,0) (1,0) (0,1) (1,1).
    let corner = vec2f(f32(vi & 1u), f32((vi >> 1u) & 1u));
    // Grow the quad by twice the pixel footprint's half extent in local units, so every pixel
    // the shape touches is rasterized; extra pixels get coverage 0.
    let half_px = 0.5 * vec2f(abs(inst.inv.x) + abs(inst.inv.z), abs(inst.inv.y) + abs(inst.inv.w));
    let margin = 2.0 * half_px;
    let p = inst.rect.xy - margin + corner * (inst.rect.zw + 2.0 * margin);
    var o: VOut;
    o.pos = vec4f(inst.xform.x * p.x + inst.xform.z * p.y + inst.xlate.x,
                  inst.xform.y * p.x + inst.xform.w * p.y + inst.xlate.y, 0.0, 1.0);
    o.local = p;
    o.rect = inst.rect;
    o.radii = inst.radii;
    o.color = inst.color;
    o.inv = inst.inv;
    o.flags = u32(inst.xlate.z);
    o.clip = u32(inst.xlate.w);
    return o;
}

// Area of the unit pixel square on the inner side of a straight edge. d: signed distance in
// device pixels from the pixel centre to the edge, positive inside. n: the edge's unit normal.
fn edge_coverage(d: f32, n: vec2f) -> f32 {
    let lo = min(abs(n.x), abs(n.y));
    let hi = max(abs(n.x), abs(n.y));
    let t = d + 0.5 * (lo + hi);
    if (lo < 1e-4) {
        return clamp(t / hi, 0.0, 1.0);
    }
    if (t <= 0.0) {
        return 0.0;
    }
    if (t >= lo + hi) {
        return 1.0;
    }
    if (t < lo) {
        return t * t / (2.0 * lo * hi);
    }
    if (t <= hi) {
        return (t - 0.5 * lo) / hi;
    }
    let u = lo + hi - t;
    return 1.0 - u * u / (2.0 * lo * hi);
}

// Coverage of the slab lo_edge < x < hi_edge, for a local coordinate x whose gradient in device
// pixels is g.
fn slab_coverage(x: f32, lo_edge: f32, hi_edge: f32, g: vec2f) -> f32 {
    let len = max(length(g), 1e-20);
    let n = g / len;
    return max(edge_coverage((x - lo_edge) / len, n) + edge_coverage((hi_edge - x) / len, n) - 1.0,
               0.0);
}

fn half_plane(d_local: f32, g: vec2f) -> f32 {
    let len = max(length(g), 1e-20);
    return edge_coverage(d_local / len, g / len);
}

// Coverage of one rounded corner: `vertex` is the rect's corner, `inward` points into the rect
// (+-1 per axis). 1 when the pixel centre is not beyond the corner's centre; otherwise the
// circle's coverage, its edge treated as locally straight.
fn corner_coverage(p: vec2f, vertex: vec2f, r: f32, inward: vec2f, inv: vec4f) -> f32 {
    if (r <= 0.0) {
        return 1.0;
    }
    let c = vertex + inward * r;
    let beyond = (c - p) * inward; // positive on both axes beyond the centre, toward the vertex
    if (beyond.x <= 0.0 || beyond.y <= 0.0) {
        return 1.0;
    }
    let v = p - c;
    let dist = length(v);
    let g = select(vec2f(1.0, 0.0), v / dist, dist > 1e-6);
    // Gradient of the distance to the centre, in device pixels.
    let grad = vec2f(g.x * inv.x + g.y * inv.y, g.x * inv.z + g.y * inv.w);
    return half_plane(r - dist, grad);
}

// Coverage of a rect, or a rounded rect with `rounded`, at local point p whose footprint in device
// pixels is given by inv.
fn shape_coverage(p: vec2f, rect: vec4f, radii: vec4f, rounded: bool, inv: vec4f) -> f32 {
    let gx = vec2f(inv.x, inv.z); // gradient of local x in device pixels
    let gy = vec2f(inv.y, inv.w); // gradient of local y
    let x0 = rect.x;
    let y0 = rect.y;
    let x1 = rect.x + rect.z;
    let y1 = rect.y + rect.w;

    var cov = slab_coverage(p.x, x0, x1, gx) * slab_coverage(p.y, y0, y1, gy);

    if (rounded) {
        // A corner's arc applies where the pixel centre lies beyond the corner's centre on both
        // axes. Check all four: with CSS radii one corner's arc can reach past the middle of a
        // side, and opposite corners' regions can overlap.
        cov = min(cov, corner_coverage(p, vec2f(x0, y0), radii.x, vec2f(1.0, 1.0), inv));
        cov = min(cov, corner_coverage(p, vec2f(x1, y0), radii.y, vec2f(-1.0, 1.0), inv));
        cov = min(cov, corner_coverage(p, vec2f(x1, y1), radii.z, vec2f(-1.0, -1.0), inv));
        cov = min(cov, corner_coverage(p, vec2f(x0, y1), radii.w, vec2f(1.0, -1.0), inv));
    }
    return cov;
}

@fragment
fn fs(in: VOut) -> @location(0) vec4f {
    // Derivatives in uniform control flow; only used with FLAG_FOOTPRINT_FROM_DERIVATIVES.
    let ddx = dpdxFine(in.local);
    let ddy = dpdyFine(in.local);
    var inv = in.inv;
    if ((in.flags & FLAG_FOOTPRINT_FROM_DERIVATIVES) != 0u) {
        inv = vec4f(ddx.x, ddx.y, ddy.x, ddy.y);
    }
    var cov = shape_coverage(in.local, in.rect, in.radii, (in.flags & FLAG_ROUNDED) != 0u, inv);

    // The clips, innermost first, each in its own space (the pixel centre is in.pos.xy).
    var ci = in.clip;
    for (var n = 0u; n < MAX_CLIPS && ci != 0u; n = n + 1u) {
        let c = clips[ci - 1u];
        let q = vec2f(c.m.x * in.pos.x + c.m.z * in.pos.y + c.t.x,
                      c.m.y * in.pos.x + c.m.w * in.pos.y + c.t.y);
        cov = cov * shape_coverage(q, c.rect, c.radii, (u32(c.t.z) & FLAG_ROUNDED) != 0u, c.m);
        ci = u32(c.t.w);
    }
    return in.color * cov;
}
