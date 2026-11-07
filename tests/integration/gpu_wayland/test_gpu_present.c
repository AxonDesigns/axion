/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_gpu_surface against a headless weston, on a software Vulkan driver (lavapipe): frames are
 * presented in both pacing modes, resizes reconfigure the swapchain to the acknowledged size, and
 * presentation survives a device loss. The test thread is the main thread and pumps; a presenter
 * thread owns the ax_gpu.
 *
 * CMake points the Vulkan loader at lavapipe for these tests (VK_DRIVER_FILES), so they never
 * depend on the machine's GPU driver supporting a headless compositor.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ax_test.h"
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_thread.h"
#include "axion/gpu/ax_gpu_surface.h"
#include "gpu_test_util.h"
#include "gpu_testing.h"
#include "wayland/wl_testing.h"
#include "wl_test_util.h"

typedef struct presenter {
    ax_surface surface;
    ax_gpu_pacing pacing;
    uint32_t lose_device_at; /* frame number, 0 = never */

    ax_atomic_u32 stop;
    ax_atomic_u32 state; /* 0 starting, 1 running, 2 no adapter, 3 failed */
    ax_atomic_u32 frames;
    ax_atomic_u32 size_mismatches; /* texture size != the configure's pixel size */
    ax_atomic_u32 failures;

    /* final, read after join */
    ax_present_stats present;
    ax_gpu_surface_stats gpu;
    ax_gpu_surface_info info;
    uint32_t generation;
    uint32_t uncaptured_errors;
    uint32_t frames_before_loss;
    uint64_t presented_before_loss;
} presenter;

static void clear(ax_gpu *gpu, WGPUTextureView view, uint32_t n)
{
    WGPUDevice device = ax_gpu_device(gpu);
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
    WGPURenderPassEncoder pass;
    WGPUCommandBuffer commands;

    color.view = view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue.r = (double)(n % 64u) / 64.0;
    color.clearValue.g = 0.3;
    color.clearValue.b = 0.6;
    color.clearValue.a = 1.0;
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color;
    pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(ax_gpu_queue(gpu), 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
}

static int presenter_main(void *arg)
{
    presenter *p = (presenter *)arg;
    ax_gpu_desc gdesc = {sizeof gdesc, NULL, AX_GPU_ADAPTER_SOFTWARE};
    ax_gpu_surface_desc sdesc = {sizeof sdesc, AX_GPU_PACING_DEFAULT, WGPUPresentMode_Undefined};
    ax_gpu *gpu = NULL;
    ax_gpu_surface *surface = NULL;
    uint32_t n = 0;
    ax_result r;

    axt_gpu_pin_drivers();
    r = ax_gpu_create(&gdesc, &gpu);
    if (r != AX_OK) {
        ax_atomic_store_u32(&p->state, r == AX_ERR_UNSUPPORTED ? 2u : 3u, AX_MO_RELEASE);
        return 0;
    }
    sdesc.pacing = p->pacing;
    if (ax_gpu_surface_create(gpu, p->surface, &sdesc, &surface) != AX_OK) {
        ax_gpu_destroy(gpu);
        ax_atomic_store_u32(&p->state, 3, AX_MO_RELEASE);
        return 0;
    }
    ax_atomic_store_u32(&p->state, 1, AX_MO_RELEASE);

    while (ax_atomic_load_u32(&p->stop, AX_MO_ACQUIRE) == 0) {
        ax_gpu_frame f;
        memset(&f, 0, sizeof f);
        f.struct_size = sizeof f;
        r = ax_gpu_surface_begin_frame(surface, &f);
        if (r != AX_OK && r != AX_ERR_DEVICE_LOST) {
            ax_atomic_fetch_add_u32(&p->failures, 1, AX_MO_RELAXED);
            break;
        }
        if (f.texture != NULL) {
            if (f.width != f.target.pixel_width || f.height != f.target.pixel_height ||
                wgpuTextureGetWidth(f.texture) != f.target.pixel_width ||
                wgpuTextureGetHeight(f.texture) != f.target.pixel_height) {
                ax_atomic_fetch_add_u32(&p->size_mismatches, 1, AX_MO_RELAXED);
            }
            clear(gpu, f.view, n);
            if (ax_gpu_surface_present(surface, &f) != AX_OK) {
                ax_atomic_fetch_add_u32(&p->failures, 1, AX_MO_RELAXED);
            }
            n++;
            ax_atomic_store_u32(&p->frames, n, AX_MO_RELEASE);
            if (n == p->lose_device_at) {
                ax_present_stats s;
                s.struct_size = sizeof s;
                ax_surface_get_present_stats(p->surface, &s, false);
                p->frames_before_loss = n;
                p->presented_before_loss = s.presented;
                ax__gpu_test_simulate_loss(gpu);
            }
        }
        ax_gpu_surface_wait(surface, true, 50u * 1000000u, NULL);
    }

    p->present.struct_size = sizeof p->present;
    ax_surface_get_present_stats(p->surface, &p->present, false);
    p->gpu.struct_size = sizeof p->gpu;
    ax_gpu_surface_get_stats(surface, &p->gpu, false);
    p->info.struct_size = sizeof p->info;
    ax_gpu_surface_get_info(surface, &p->info);
    p->generation = ax_gpu_device_generation(gpu);
    fprintf(
        stderr,
        "presenter: %u frames, presented %llu, discarded %llu, lost %llu, resizes %llu, "
        "acquire mean %.2f ms max %.2f ms, present mean %.2f ms max %.2f ms, drain max %.2f ms, "
        "deferred acks %llu\n",
        n, (unsigned long long)p->present.presented, (unsigned long long)p->present.discarded,
        (unsigned long long)p->present.lost_frames, (unsigned long long)p->gpu.resizes,
        n ? (double)p->gpu.acquire_ns_total / n / 1e6 : 0.0, (double)p->gpu.acquire_ns_max / 1e6,
        n ? (double)p->gpu.present_ns_total / n / 1e6 : 0.0, (double)p->gpu.present_ns_max / 1e6,
        (double)p->gpu.drain_ns_max / 1e6, (unsigned long long)p->gpu.deferred_acks);
    p->uncaptured_errors = ax_gpu_uncaptured_error_count(gpu);
    ax_gpu_surface_destroy(surface);
    ax_gpu_destroy(gpu);
    return 0;
}

typedef struct fixture {
    axt_app t;
    ax_window window;
    presenter p;
    ax_thread thread;
} fixture;

static bool started(void *ctx)
{
    return ax_atomic_load_u32(&((presenter *)ctx)->state, AX_MO_ACQUIRE) != 0;
}

typedef struct frame_goal {
    presenter *p;
    uint32_t at_least;
} frame_goal;

static bool frames_reached(void *ctx)
{
    frame_goal *g = (frame_goal *)ctx;
    return ax_atomic_load_u32(&g->p->frames, AX_MO_ACQUIRE) >= g->at_least ||
           ax_atomic_load_u32(&g->p->failures, AX_MO_ACQUIRE) != 0;
}

static bool pump_frames(fixture *f, uint32_t more)
{
    frame_goal g;
    g.p = &f->p;
    g.at_least = ax_atomic_load_u32(&f->p.frames, AX_MO_ACQUIRE) + more;
    return axt_pump_until(f->t.app, frames_reached, &g, AXT_SECONDS(20));
}

/* 1: running; 0: no software adapter (skip); -1: failed (reported). */
static int fixture_start(fixture *f, ax_gpu_pacing pacing, uint32_t lose_device_at)
{
    ax_window_desc d = axt_window_desc(320, 240, AX_DECORATIONS_NONE);
    uint32_t state;

    memset(f, 0, sizeof *f);
    if (axt_app_create(&f->t, 0) != AX_OK || ax_window_create(f->t.app, &d, &f->window) != AX_OK ||
        ax_window_get_surface(f->window, &f->p.surface) != AX_OK) {
        ax_test_fail(__FILE__, __LINE__, "app or window creation failed");
        return -1;
    }
    f->p.pacing = pacing;
    f->p.lose_device_at = lose_device_at;
    ax_atomic_init_u32(&f->p.stop, 0);
    ax_atomic_init_u32(&f->p.state, 0);
    ax_atomic_init_u32(&f->p.frames, 0);
    ax_atomic_init_u32(&f->p.size_mismatches, 0);
    ax_atomic_init_u32(&f->p.failures, 0);
    if (ax_thread_create(&f->thread, presenter_main, &f->p) != AX_OK) {
        ax_test_fail(__FILE__, __LINE__, "thread creation failed");
        return -1;
    }
    /* Surface creation may roundtrip on the display: the main thread pumps meanwhile. */
    axt_pump_until(f->t.app, started, &f->p, AXT_SECONDS(20));
    state = ax_atomic_load_u32(&f->p.state, AX_MO_ACQUIRE);
    if (state == 1) {
        return 1;
    }
    ax_thread_join(&f->thread);
    ax_window_destroy(f->window);
    axt_app_destroy(&f->t);
    if (state == 2) {
        const char *req = getenv("AX_REQUIRE_GPU_TESTS");
        if (req != NULL && strcmp(req, "1") == 0) {
            ax_test_fail(__FILE__, __LINE__, "AX_REQUIRE_GPU_TESTS=1 but no software adapter");
            return -1;
        }
        return 0;
    }
    ax_test_fail(__FILE__, __LINE__, "the presenter failed to start (state %u)", state);
    return -1;
}

static size_t fixture_stop(fixture *f)
{
    /* The presenter reads the display itself if its teardown needs a roundtrip; the main thread
     * does not have to pump while it joins. */
    ax_atomic_store_u32(&f->p.stop, 1, AX_MO_RELEASE);
    ax_thread_join(&f->thread);
    ax_window_destroy(f->window);
    return axt_app_destroy(&f->t);
}

static bool is_srgb_format(WGPUTextureFormat format)
{
    return format == WGPUTextureFormat_BGRA8UnormSrgb || format == WGPUTextureFormat_RGBA8UnormSrgb;
}

#define START(f, pacing, lose_at)                                                        \
    do {                                                                                 \
        int axt__s;                                                                      \
        AXT_REQUIRE_COMPOSITOR();                                                        \
        axt__s = fixture_start((f), (pacing), (lose_at));                                \
        if (axt__s == 0) {                                                               \
            AX_SKIP("no software Vulkan adapter (lavapipe) for the presentation tests"); \
        }                                                                                \
        if (axt__s < 0) {                                                                \
            return;                                                                      \
        }                                                                                \
    } while (0)

static void check_presented(fixture *f, uint32_t frames)
{
    presenter *p = &f->p;
    AX_CHECK_EQ_U(ax_atomic_load_u32(&p->failures, AX_MO_RELAXED), 0);
    AX_CHECK(ax_atomic_load_u32(&p->frames, AX_MO_RELAXED) >= frames);
    AX_CHECK_EQ_U(ax_atomic_load_u32(&p->size_mismatches, AX_MO_RELAXED), 0);
    /* Presentation feedback arrives for the GPU's commits as for shared-memory ones. */
    AX_CHECK(p->present.presented >= frames / 2);
    AX_CHECK_EQ_U(p->present.lost_frames, 0);
    AX_CHECK_EQ_U(p->gpu.failed_presents, 0);
    AX_CHECK_EQ_U(p->uncaptured_errors, 0);
    AX_CHECK(is_srgb_format(p->info.view_format));
}

AX_TEST(frames_are_presented_with_compositor_pacing)
{
    fixture f;
    START(&f, AX_GPU_PACING_COMPOSITOR, 0);
    AX_CHECK(pump_frames(&f, 40));
    AX_CHECK_EQ_U(fixture_stop(&f), 0);
    check_presented(&f, 40);
    AX_CHECK_EQ_U(f.p.info.pacing, AX_GPU_PACING_COMPOSITOR);
    AX_CHECK_EQ_U(f.p.info.present_mode, WGPUPresentMode_Mailbox);
}

AX_TEST(frames_are_presented_with_driver_pacing)
{
    fixture f;
    START(&f, AX_GPU_PACING_DRIVER, 0);
    AX_CHECK(pump_frames(&f, 40));
    AX_CHECK_EQ_U(fixture_stop(&f), 0);
    check_presented(&f, 40);
    AX_CHECK_EQ_U(f.p.info.pacing, AX_GPU_PACING_DRIVER);
    AX_CHECK_EQ_U(f.p.info.present_mode, WGPUPresentMode_Fifo);
}

static void check_resizes(ax_gpu_pacing pacing)
{
    fixture f;
    START(&f, pacing, 0);
    AX_CHECK(pump_frames(&f, 10));
    ax__wl_test_set_maximized(f.window, true);
    AX_CHECK(pump_frames(&f, 20));
    ax__wl_test_set_maximized(f.window, false);
    AX_CHECK(pump_frames(&f, 20));
    AX_CHECK_EQ_U(fixture_stop(&f), 0);

    check_presented(&f, 50);
    /* Two configures with new sizes: each resized the swapchain once, before its frame, and the
     * frame acknowledging it reached the screen. */
    AX_CHECK_EQ_U(f.p.gpu.resizes, 2);
    AX_CHECK(f.p.present.latency_samples >= 2);
    AX_CHECK_EQ_U(f.p.info.width, 320);
    AX_CHECK_EQ_U(f.p.info.height, 240);
}

AX_TEST(resizes_reconfigure_the_swapchain_with_compositor_pacing)
{
    check_resizes(AX_GPU_PACING_COMPOSITOR);
}

/* With FIFO and wp_fifo_v1, Mesa's swapchain commits wait in the compositor's queue; weston 15
 * applies a configure acknowledgement on receipt and then rejects the older, queued commits
 * (decision 48, docs/upstream/weston-fifo-ack-configure.md). Without wp_fifo_v1 nothing queues and
 * this test would pass without testing the drain, so it skips, unless AX_REQUIRE_WP_FIFO=1 (the
 * CI job with a newer weston) makes that a failure. */
AX_TEST(resizes_reconfigure_the_swapchain_with_driver_pacing)
{
    AXT_REQUIRE_COMPOSITOR();
    if (!axt_compositor_has_global("wp_fifo_manager_v1")) {
        const char *req = getenv("AX_REQUIRE_WP_FIFO");
        if (req != NULL && strcmp(req, "1") == 0) {
            ax_test_fail(__FILE__, __LINE__,
                         "AX_REQUIRE_WP_FIFO=1 but the compositor does not announce "
                         "wp_fifo_manager_v1");
            return;
        }
        AX_SKIP("the compositor lacks wp_fifo_v1: no commit can queue, so the drain before "
                "acknowledging a configure is not exercised (needs weston 15 or newer)");
    }
    check_resizes(AX_GPU_PACING_DRIVER);
}

AX_TEST(presentation_continues_after_a_device_loss)
{
    fixture f;
    START(&f, AX_GPU_PACING_COMPOSITOR, 10);
    AX_CHECK(pump_frames(&f, 40));
    AX_CHECK_EQ_U(fixture_stop(&f), 0);

    check_presented(&f, 40);
    AX_CHECK_EQ_U(f.p.generation, 2);
    AX_CHECK_EQ_U(f.p.gpu.recreations, 1);
    AX_CHECK_EQ_U(f.p.frames_before_loss, 10);
    AX_CHECK(f.p.present.presented > f.p.presented_before_loss + 10);
}
