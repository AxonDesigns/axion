/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_render_wgpu's glyph atlas (decisions 100 and 101): A8 pages of a 2D array texture, filled on
 * demand from the snapshot's glyph source, on the render thread (the only text work it does: the
 * glyphs were rasterized on workers).
 *
 *   - Pages are RW_ATLAS_SIZE squared, packed in shelves (rows of a height); each glyph keeps a
 *     one-texel empty border, so bilinear sampling at its edges reads zeros, as ax_render_soft
 *     does outside a bitmap.
 *   - Eviction is LRU by page: when no page has room, the page least recently used (in frames)
 *     is emptied, unless the current frame uses it; then the glyph is skipped and counted.
 *   - The table from glyph key to place is open addressing; emptying a page rebuilds it.
 *   - The coverage correction table (ax_ir_text_lut) is a small texture loaded once per device.
 */
#include <string.h>

#include <axion/base/ax_memory.h>

#include "rw_internal.h"

static uint32_t key_hash(uint32_t instance, uint32_t glyph, uint32_t bin)
{
    uint32_t h = instance * 2654435761u;
    h ^= glyph * 40503u + 0x9E3779B9u;
    h ^= bin * 0x85EBCA6Bu;
    h ^= h >> 15;
    return h * 0x2C1B3C6Du;
}

static void release_textures(rw_atlas *a)
{
    if (a->view != NULL) {
        wgpuTextureViewRelease(a->view);
    }
    if (a->texture != NULL) {
        wgpuTextureRelease(a->texture);
    }
    if (a->lut_view != NULL) {
        wgpuTextureViewRelease(a->lut_view);
    }
    if (a->lut != NULL) {
        wgpuTextureRelease(a->lut);
    }
    if (a->sampler != NULL) {
        wgpuSamplerRelease(a->sampler);
    }
    a->view = NULL;
    a->texture = NULL;
    a->lut_view = NULL;
    a->lut = NULL;
    a->sampler = NULL;
}

static void clear_places(rw_atlas *a)
{
    uint32_t p;
    memset(a->table, 0, sizeof a->table);
    a->entry_count = 0;
    for (p = 0; p < RW_ATLAS_PAGES; ++p) {
        a->pages[p].shelf_count = 0;
        a->pages[p].last_frame = 0;
    }
}

void ax__rw_atlas_free(ax_render_wgpu *r)
{
    release_textures(&r->atlas);
}

/* Textures for the current device (again after a device loss). */
static bool ensure_textures(ax_render_wgpu *r)
{
    rw_atlas *a = &r->atlas;
    const uint32_t gen = ax_gpu_device_generation(r->gpu);
    WGPUDevice device = ax_gpu_device(r->gpu);
    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    WGPUTextureViewDescriptor vd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUSamplerDescriptor sd = WGPU_SAMPLER_DESCRIPTOR_INIT;
    if (a->texture != NULL && a->generation == gen) {
        return true;
    }
    release_textures(a);
    clear_places(a);
    a->generation = gen;
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.dimension = WGPUTextureDimension_2D;
    td.size.width = RW_ATLAS_SIZE;
    td.size.height = RW_ATLAS_SIZE;
    td.size.depthOrArrayLayers = RW_ATLAS_PAGES;
    td.format = WGPUTextureFormat_R8Unorm;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    a->texture = wgpuDeviceCreateTexture(device, &td);
    vd.dimension = WGPUTextureViewDimension_2DArray;
    vd.arrayLayerCount = RW_ATLAS_PAGES;
    vd.mipLevelCount = 1;
    vd.format = WGPUTextureFormat_R8Unorm;
    a->view = a->texture != NULL ? wgpuTextureCreateView(a->texture, &vd) : NULL;

    td.size.width = 256;
    td.size.height = AX_IR_TEXT_LUT_BUCKETS;
    td.size.depthOrArrayLayers = 1;
    a->lut = wgpuDeviceCreateTexture(device, &td);
    vd.dimension = WGPUTextureViewDimension_2D;
    vd.arrayLayerCount = 1;
    a->lut_view = a->lut != NULL ? wgpuTextureCreateView(a->lut, &vd) : NULL;

    sd.addressModeU = WGPUAddressMode_ClampToEdge;
    sd.addressModeV = WGPUAddressMode_ClampToEdge;
    sd.addressModeW = WGPUAddressMode_ClampToEdge;
    sd.magFilter = WGPUFilterMode_Linear;
    sd.minFilter = WGPUFilterMode_Linear;
    sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    sd.maxAnisotropy = 1;
    a->sampler = wgpuDeviceCreateSampler(device, &sd);
    if (a->view == NULL || a->lut_view == NULL || a->sampler == NULL) {
        release_textures(a);
        return false;
    }
    {
        /* The coverage correction, the same table as ax_render_soft's. */
        uint8_t lut[AX_IR_TEXT_LUT_BUCKETS][256];
        const ax_ir_text_lut_params lp = ax_ir_text_lut_default();
        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        WGPUExtent3D extent = {256, AX_IR_TEXT_LUT_BUCKETS, 1};
        ax_ir_text_lut_build(&lp, lut);
        dst.texture = a->lut;
        layout.bytesPerRow = 256;
        layout.rowsPerImage = AX_IR_TEXT_LUT_BUCKETS;
        wgpuQueueWriteTexture(ax_gpu_queue(r->gpu), &dst, lut, sizeof lut, &layout, &extent);
    }
    return true;
}

bool ax__rw_atlas_begin(ax_render_wgpu *r)
{
    r->atlas.frame++;
    return ensure_textures(r);
}

/* A shelf place for w x h texels (border included) in page p, or false. */
static bool page_alloc(rw_atlas_page *pg, uint32_t w, uint32_t h, uint32_t *x, uint32_t *y)
{
    uint32_t i, top = 0;
    for (i = 0; i < pg->shelf_count; ++i) {
        rw_shelf *s = &pg->shelves[i];
        /* A shelf takes glyphs up to its height and not much smaller (a quarter), so short glyphs
         * do not waste tall rows. */
        if (h <= s->height && h * 4u >= s->height * 3u && s->x + w <= RW_ATLAS_SIZE) {
            *x = s->x;
            *y = s->y;
            s->x += w;
            return true;
        }
        top = s->y + s->height;
    }
    if (pg->shelf_count == RW_ATLAS_SHELVES || top + h > RW_ATLAS_SIZE || w > RW_ATLAS_SIZE) {
        return false;
    }
    pg->shelves[pg->shelf_count].y = top;
    pg->shelves[pg->shelf_count].height = h;
    pg->shelves[pg->shelf_count].x = w;
    pg->shelf_count++;
    *x = 0;
    *y = top;
    return true;
}

static rw_atlas_entry *find(rw_atlas *a, uint32_t instance, uint32_t glyph, uint32_t bin)
{
    uint32_t h = key_hash(instance, glyph, bin), i;
    for (i = 0; i < RW_ATLAS_TABLE; ++i) {
        const uint32_t idx = a->table[(h + i) & (RW_ATLAS_TABLE - 1u)];
        rw_atlas_entry *e;
        if (idx == 0) {
            return NULL;
        }
        e = &a->entries[idx - 1u];
        if (e->instance == instance && e->glyph == glyph && e->bin == bin) {
            return e;
        }
    }
    return NULL;
}

static bool insert(rw_atlas *a, uint32_t entry)
{
    const rw_atlas_entry *e = &a->entries[entry];
    uint32_t h = key_hash(e->instance, e->glyph, e->bin), i;
    for (i = 0; i < RW_ATLAS_TABLE; ++i) {
        uint32_t *slot = &a->table[(h + i) & (RW_ATLAS_TABLE - 1u)];
        if (*slot == 0) {
            *slot = entry + 1u;
            return true;
        }
    }
    return false;
}

/* Empties page p: its entries go, and the table is rebuilt from the rest. */
static void evict_page(rw_atlas *a, uint32_t p)
{
    uint32_t i, kept = 0;
    for (i = 0; i < a->entry_count; ++i) {
        if (a->entries[i].page != p) {
            a->entries[kept++] = a->entries[i];
        }
    }
    a->entry_count = kept;
    memset(a->table, 0, sizeof a->table);
    for (i = 0; i < kept; ++i) {
        (void)insert(a, i);
    }
    a->pages[p].shelf_count = 0;
    a->evictions++;
}

const rw_atlas_entry *ax__rw_atlas_glyph(ax_render_wgpu *r, const ax_ir_glyph_source *src,
                                         uint32_t instance, uint32_t glyph, uint32_t bin)
{
    rw_atlas *a = &r->atlas;
    rw_atlas_entry *e = find(a, instance, glyph, bin);
    ax_ir_glyph_bitmap bm;
    uint32_t w, h, x = 0, y = 0, p, tries;
    if (e != NULL) {
        a->pages[e->page].last_frame = a->frame;
        return e;
    }
    if (src == NULL || !src->lookup(src, instance, glyph, bin, &bm) || bm.width == 0 ||
        bm.height == 0 || bm.pixels == NULL) {
        return NULL;
    }
    w = (uint32_t)bm.width + 2u;
    h = (uint32_t)bm.height + 2u;
    if (w > RW_ATLAS_SIZE || h > RW_ATLAS_SIZE || a->entry_count == RW_ATLAS_ENTRIES) {
        a->skipped++;
        return NULL;
    }
    for (tries = 0;; ++tries) {
        bool placed = false;
        for (p = 0; p < RW_ATLAS_PAGES; ++p) {
            if (page_alloc(&a->pages[p], w, h, &x, &y)) {
                placed = true;
                break;
            }
        }
        if (placed) {
            break;
        }
        {
            /* The least recently used page not used by this frame. */
            uint32_t victim = RW_ATLAS_PAGES, q;
            for (q = 0; q < RW_ATLAS_PAGES; ++q) {
                if (a->pages[q].last_frame != a->frame &&
                    (victim == RW_ATLAS_PAGES ||
                     a->pages[q].last_frame < a->pages[victim].last_frame)) {
                    victim = q;
                }
            }
            if (victim == RW_ATLAS_PAGES || tries > 0) {
                a->skipped++;
                return NULL;
            }
            evict_page(a, victim);
        }
    }
    {
        /* The bitmap with its empty border, uploaded on the render thread. */
        uint8_t stack_buf[4096];
        uint8_t *buf = w * h <= sizeof stack_buf
                           ? stack_buf
                           : (uint8_t *)ax_mem_alloc(r->allocator, (size_t)w * h, 16);
        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        WGPUExtent3D extent;
        uint32_t row;
        if (buf == NULL) {
            return NULL;
        }
        memset(buf, 0, (size_t)w * h);
        for (row = 0; row < bm.height; ++row) {
            memcpy(buf + (size_t)(row + 1u) * w + 1u, bm.pixels + (size_t)row * bm.stride,
                   bm.width);
        }
        dst.texture = a->texture;
        dst.origin.x = x;
        dst.origin.y = y;
        dst.origin.z = p;
        layout.bytesPerRow = w;
        layout.rowsPerImage = h;
        extent.width = w;
        extent.height = h;
        extent.depthOrArrayLayers = 1;
        wgpuQueueWriteTexture(ax_gpu_queue(r->gpu), &dst, buf, (size_t)w * h, &layout, &extent);
        if (buf != stack_buf) {
            ax_mem_free(r->allocator, buf, (size_t)w * h);
        }
        a->uploads++;
    }
    e = &a->entries[a->entry_count];
    e->instance = instance;
    e->glyph = glyph;
    e->bin = bin;
    e->page = (uint16_t)p;
    e->x = (uint16_t)(x + 1u);
    e->y = (uint16_t)(y + 1u);
    e->w = bm.width;
    e->h = bm.height;
    e->left = bm.left;
    e->top = bm.top;
    if (!insert(a, a->entry_count)) {
        return NULL;
    }
    a->entry_count++;
    a->pages[p].last_frame = a->frame;
    return e;
}
