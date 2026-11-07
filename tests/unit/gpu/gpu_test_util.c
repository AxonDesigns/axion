/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#if defined(__linux__)
#    define _POSIX_C_SOURCE 200809L
#endif

#include "gpu_test_util.h"

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
