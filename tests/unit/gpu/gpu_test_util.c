/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#if defined(__linux__)
#    define _POSIX_C_SOURCE 200809L
#endif

#include "gpu_test_util.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "axion/ax_error.h"

static bool env_is(const char *name, const char *value)
{
    const char *v = getenv(name);
    return v != NULL && strcmp(v, value) == 0;
}

int axt_gpu_create(const ax_allocator *allocator, ax_gpu **out_gpu)
{
    ax_gpu_desc desc = {sizeof desc, NULL, AX_GPU_ADAPTER_DEFAULT};
    ax_result r;

    axt_gpu_pin_drivers();
    desc.allocator = allocator;
    if (env_is("AX_TEST_GPU_ADAPTER", "software")) {
        desc.adapter = AX_GPU_ADAPTER_SOFTWARE;
    }
    r = ax_gpu_create(&desc, out_gpu);
    if (r == AX_OK) {
        return 1;
    }
    if (r == AX_ERR_UNSUPPORTED && !env_is("AX_REQUIRE_GPU_TESTS", "1")) {
        return 0;
    }
    ax_test_fail(__FILE__, __LINE__, "ax_gpu_create: %s: %s", ax_result_name(r),
                 ax_last_error()->message.ptr);
    return -1;
}

/* ---- Clear and read back ---------------------------------------------------------------- */

typedef struct map_result {
    bool done;
    WGPUMapAsyncStatus status;
} map_result;

static void on_mapped(WGPUMapAsyncStatus status, WGPUStringView message, void *userdata1,
                      void *userdata2)
{
    map_result *r = userdata1;
    (void)message;
    (void)userdata2;
    r->done = true;
    r->status = status;
}

enum { CLEAR_SIZE = 64, CLEAR_ROW_BYTES = CLEAR_SIZE * 4 }; /* 256: copy rows need 256 alignment */

static bool near(uint8_t got, unsigned expected)
{
    return got + 1u >= expected && got <= expected + 1u;
}

bool axt_gpu_clear_and_read_back(ax_gpu *gpu)
{
    WGPUDevice device = ax_gpu_device(gpu);
    WGPUTextureDescriptor tdesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    WGPUBufferDescriptor bdesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    WGPUExtent3D extent = {CLEAR_SIZE, CLEAR_SIZE, 1};
    WGPUBufferMapCallbackInfo map_cb = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    map_result mapped = {false, WGPUMapAsyncStatus_Error};
    WGPUTexture texture;
    WGPUTextureView view;
    WGPUBuffer buffer;
    WGPUCommandEncoder encoder;
    WGPURenderPassEncoder pass;
    WGPUCommandBuffer commands;
    const uint8_t *pixels;
    uint32_t bad = 0;
    uint32_t x, y;

    tdesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    tdesc.size = extent;
    tdesc.format = WGPUTextureFormat_RGBA8Unorm;
    texture = wgpuDeviceCreateTexture(device, &tdesc);
    if (texture == NULL) {
        ax_test_fail(__FILE__, __LINE__, "texture creation failed");
        return false;
    }
    view = wgpuTextureCreateView(texture, NULL);

    bdesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bdesc.size = (uint64_t)CLEAR_ROW_BYTES * CLEAR_SIZE;
    buffer = wgpuDeviceCreateBuffer(device, &bdesc);
    if (buffer == NULL) {
        ax_test_fail(__FILE__, __LINE__, "buffer creation failed");
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(texture);
        return false;
    }

    color.view = view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue.r = 0.25;
    color.clearValue.g = 0.5;
    color.clearValue.b = 0.75;
    color.clearValue.a = 1.0;
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color;

    encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
    pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    src.texture = texture;
    dst.buffer = buffer;
    dst.layout.bytesPerRow = CLEAR_ROW_BYTES;
    dst.layout.rowsPerImage = CLEAR_SIZE;
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);
    commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(ax_gpu_queue(gpu), 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);

    map_cb.mode = WGPUCallbackMode_AllowProcessEvents;
    map_cb.callback = on_mapped;
    map_cb.userdata1 = &mapped;
    if (!ax_gpu_wait(gpu,
                     wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, (size_t)bdesc.size, map_cb),
                     &mapped.done, 5000000000ull) ||
        mapped.status != WGPUMapAsyncStatus_Success) {
        ax_test_fail(__FILE__, __LINE__, "mapping the read-back buffer failed");
        bad = 1;
    }

    pixels = bad == 0 ? wgpuBufferGetConstMappedRange(buffer, 0, (size_t)bdesc.size) : NULL;
    for (y = 0; pixels != NULL && y < CLEAR_SIZE; ++y) {
        for (x = 0; x < CLEAR_SIZE; ++x) {
            const uint8_t *p = pixels + (size_t)y * CLEAR_ROW_BYTES + (size_t)x * 4;
            /* 0.25, 0.5, 0.75 in UNORM8: 63.75, 127.5, 191.25, rounded either way. */
            if (!near(p[0], 64) || !near(p[1], 128) || !near(p[2], 191) || p[3] != 255) {
                if (bad++ == 0) {
                    ax_test_fail(__FILE__, __LINE__, "pixel (%u, %u) = %u %u %u %u", x, y, p[0],
                                 p[1], p[2], p[3]);
                }
            }
        }
    }
    if (pixels != NULL) {
        wgpuBufferUnmap(buffer);
    }

    wgpuBufferRelease(buffer);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(texture);
    return bad == 0;
}

/* ---- Driver pinning ------------------------------------------------------------------------ */

#if defined(__linux__)
#    include <dlfcn.h>
#    include <stddef.h>
#    include <stdint.h>

/* The few Vulkan declarations needed, so the tests do not depend on Vulkan headers. */
typedef struct axt_vk_instance_create_info {
    int32_t s_type; /* VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1 */
    const void *next;
    uint32_t flags;
    const void *application_info;
    uint32_t layer_count;
    const char *const *layers;
    uint32_t extension_count;
    const char *const *extensions;
} axt_vk_instance_create_info;
typedef int32_t (*axt_vk_create_instance_fn)(const axt_vk_instance_create_info *, const void *,
                                             void **);
typedef void (*(*axt_vk_get_instance_proc_addr_fn)(void *, const char *))(void);

void axt_gpu_pin_drivers(void)
{
    static int pinned;
    axt_vk_get_instance_proc_addr_fn get_proc;
    axt_vk_create_instance_fn create;
    axt_vk_instance_create_info info = {1, NULL, 0, NULL, 0, NULL, 0, NULL};
    void *instance = NULL;
    void *lib;

    if (pinned) {
        return;
    }
    pinned = 1;
    lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (lib == NULL) {
        return;
    }
    /* POSIX's way to read a function pointer from dlsym without an object-to-function cast. */
    *(void **)&get_proc = dlsym(lib, "vkGetInstanceProcAddr");
    if (get_proc == NULL) {
        return;
    }
    create = (axt_vk_create_instance_fn)get_proc(NULL, "vkCreateInstance");
    if (create != NULL) {
        (void)create(&info, NULL, &instance); /* deliberately never destroyed */
    }
}
#else
void axt_gpu_pin_drivers(void)
{
}
#endif
