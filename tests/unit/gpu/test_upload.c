/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_gpu_upload_ring: data pushed reaches the GPU buffer at the offsets reported, frames reuse
 * the buffer, it grows when a frame needs more, and it is recreated after a device loss.
 */
#include <string.h>

#include "ax_test.h"
#include "axion/gpu/ax_gpu_upload.h"
#include "gpu_test_util.h"
#include "gpu_testing.h"

static void fill(uint32_t *words, uint32_t count, uint32_t seed)
{
    uint32_t i;
    for (i = 0; i < count; ++i) {
        words[i] = seed * 2654435761u + i;
    }
}

static bool slice_holds(ax_gpu *gpu, const ax_gpu_upload_slice *s, const uint32_t *words)
{
    static uint32_t back[16384];
    if (s->size > sizeof back || !axt_gpu_read_buffer(gpu, s->buffer, s->offset, s->size, back)) {
        return false;
    }
    return memcmp(back, words, (size_t)s->size) == 0;
}

AX_TEST(pushes_land_at_aligned_offsets)
{
    ax_gpu *gpu = NULL;
    ax_gpu_upload_ring *ring = NULL;
    ax_gpu_upload_slice a, b, c;
    uint32_t wa[3], wb[64], wc[5];
    ax_gpu_upload_stats st = {sizeof st, 0, 0, 0, 0, 0};

    AXT_GPU_CREATE(NULL, &gpu);
    AX_REQUIRE(ax_gpu_upload_ring_create(gpu, WGPUBufferUsage_Vertex | WGPUBufferUsage_CopySrc,
                                         4096, &ring) == AX_OK);
    AX_REQUIRE(ax_gpu_upload_ring_begin_frame(ring) == AX_OK);
    fill(wa, 3, 1);
    fill(wb, 64, 2);
    fill(wc, 5, 3);
    AX_REQUIRE(ax_gpu_upload_ring_push(ring, wa, sizeof wa, 4, &a) == AX_OK);
    AX_REQUIRE(ax_gpu_upload_ring_push(ring, wb, sizeof wb, 256, &b) == AX_OK);
    AX_REQUIRE(ax_gpu_upload_ring_push(ring, wc, sizeof wc, 16, &c) == AX_OK);
    AX_CHECK_EQ_U(a.offset, 0);
    AX_CHECK_EQ_U(b.offset, 256);
    AX_CHECK_EQ_U(c.offset, 256 + 256);
    AX_CHECK_EQ_P(a.buffer, b.buffer);
    AX_CHECK(slice_holds(gpu, &a, wa));
    AX_CHECK(slice_holds(gpu, &b, wb));
    AX_CHECK(slice_holds(gpu, &c, wc));

    /* The next frame starts at offset 0 again, in the same buffer. */
    AX_REQUIRE(ax_gpu_upload_ring_begin_frame(ring) == AX_OK);
    fill(wb, 64, 9);
    AX_REQUIRE(ax_gpu_upload_ring_push(ring, wb, sizeof wb, 16, &c) == AX_OK);
    AX_CHECK_EQ_U(c.offset, 0);
    AX_CHECK_EQ_P(c.buffer, a.buffer);
    AX_CHECK(slice_holds(gpu, &c, wb));
    ax_gpu_upload_ring_get_stats(ring, &st);
    AX_CHECK_EQ_U(st.capacity, 4096);
    AX_CHECK_EQ_U(st.frame_bytes, sizeof wb);
    AX_CHECK_EQ_U(st.peak_bytes, sizeof wa + sizeof wb + sizeof wc);
    AX_CHECK_EQ_U(st.grows, 0);

    AX_CHECK_EQ_U(ax_gpu_uncaptured_error_count(gpu), 0);
    ax_gpu_upload_ring_destroy(ring);
    ax_gpu_destroy(gpu);
}

AX_TEST(grows_and_keeps_earlier_slices_valid)
{
    static uint32_t big[12000];
    ax_gpu *gpu = NULL;
    ax_gpu_upload_ring *ring = NULL;
    ax_gpu_upload_slice first, second;
    uint32_t small[100];
    ax_gpu_upload_stats st = {sizeof st, 0, 0, 0, 0, 0};

    AXT_GPU_CREATE(NULL, &gpu);
    AX_REQUIRE(ax_gpu_upload_ring_create(gpu, WGPUBufferUsage_Vertex | WGPUBufferUsage_CopySrc,
                                         1024, &ring) == AX_OK);
    AX_REQUIRE(ax_gpu_upload_ring_begin_frame(ring) == AX_OK);
    fill(small, 100, 4);
    fill(big, 12000, 5);
    AX_REQUIRE(ax_gpu_upload_ring_push(ring, small, sizeof small, 4, &first) == AX_OK);
    AX_REQUIRE(ax_gpu_upload_ring_push(ring, big, sizeof big, 4, &second) == AX_OK);
    AX_CHECK(second.buffer != first.buffer);
    AX_CHECK_EQ_U(second.offset, 0);
    /* Both slices hold their data, the first in the retired buffer. */
    AX_CHECK(slice_holds(gpu, &first, small));
    AX_CHECK(slice_holds(gpu, &second, big));
    ax_gpu_upload_ring_get_stats(ring, &st);
    AX_CHECK_EQ_U(st.grows, 1);
    AX_CHECK(st.capacity >= sizeof big);

    /* The grown buffer is kept: the next frame fits without growing. */
    AX_REQUIRE(ax_gpu_upload_ring_begin_frame(ring) == AX_OK);
    AX_REQUIRE(ax_gpu_upload_ring_push(ring, big, sizeof big, 4, &second) == AX_OK);
    ax_gpu_upload_ring_get_stats(ring, &st);
    AX_CHECK_EQ_U(st.grows, 1);
    AX_CHECK(slice_holds(gpu, &second, big));

    AX_CHECK_EQ_U(ax_gpu_uncaptured_error_count(gpu), 0);
    ax_gpu_upload_ring_destroy(ring);
    ax_gpu_destroy(gpu);
}

AX_TEST(recreated_after_a_device_loss)
{
    ax_gpu *gpu = NULL;
    ax_gpu_upload_ring *ring = NULL;
    ax_gpu_upload_slice s;
    uint32_t w[8];
    ax_gpu_upload_stats st = {sizeof st, 0, 0, 0, 0, 0};

    AXT_GPU_CREATE(NULL, &gpu);
    AX_REQUIRE(ax_gpu_upload_ring_create(gpu, WGPUBufferUsage_Vertex | WGPUBufferUsage_CopySrc, 0,
                                         &ring) == AX_OK);
    ax__gpu_test_simulate_loss(gpu);
    AX_REQUIRE(ax_gpu_poll(gpu) == AX_OK);
    AX_REQUIRE(ax_gpu_device_generation(gpu) == 2);
    AX_REQUIRE(ax_gpu_upload_ring_begin_frame(ring) == AX_OK);
    fill(w, 8, 6);
    AX_REQUIRE(ax_gpu_upload_ring_push(ring, w, sizeof w, 4, &s) == AX_OK);
    AX_CHECK(slice_holds(gpu, &s, w)); /* read through the new device */
    ax_gpu_upload_ring_get_stats(ring, &st);
    AX_CHECK_EQ_U(st.recreations, 1);
    AX_CHECK_EQ_U(st.capacity, 65536);
    ax_gpu_upload_ring_destroy(ring);
    ax_gpu_destroy(gpu);
}
