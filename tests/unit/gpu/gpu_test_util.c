/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#if defined(__linux__)
#    define _POSIX_C_SOURCE 200809L
#endif

#include "gpu_test_util.h"

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
