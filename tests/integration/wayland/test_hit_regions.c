/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Hit-region maps: published from any thread, read by the main thread without waiting.
 *
 * The pointer press that turns a region into xdg_toplevel.move/resize cannot be exercised here:
 * a headless compositor has no input devices. The lookup itself is unit-tested
 * (tests/unit/platform/test_hit_test.c) and the interactive path is checked on a desktop with
 * examples/m0_window --decorations=none.
 */
#include <string.h>

#include "ax_test.h"
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_thread.h"
#include "axion/platform/ax_hit_regions.h"
#include "common/platform_internal.h"
#include "wl_test_util.h"

enum { PUBLISHES = 20000 };

typedef struct publisher {
    ax_window window;
    ax_atomic_u32 done;
    ax_atomic_u32 failures;
} publisher;

/* Map number i has (i % 64) + 1 regions, every one with x == i: a torn map would mix values. */
static void make_map(ax_hit_region *regions, uint32_t i, uint32_t *count)
{
    uint32_t k;
    *count = (i % AX_HIT_REGIONS_MAX) + 1u;
    for (k = 0; k < *count; ++k) {
        regions[k].x = (float)i;
        regions[k].y = 0;
        regions[k].width = 10;
        regions[k].height = 10;
        regions[k].region = AX_REGION_CAPTION;
    }
}

static int publish_many(void *arg)
{
    publisher *p = (publisher *)arg;
    ax_hit_region regions[AX_HIT_REGIONS_MAX];
    uint32_t i;
    for (i = 1; i <= PUBLISHES; ++i) {
        uint32_t count;
        make_map(regions, i, &count);
        if (ax_window_set_hit_regions(p->window, regions, count) != AX_OK) {
            ax_atomic_fetch_add_u32(&p->failures, 1, AX_MO_RELAXED);
        }
    }
    ax_atomic_store_u32(&p->done, 1, AX_MO_RELEASE);
    return 0;
}

AX_TEST(maps_published_from_another_thread_are_read_whole_and_latest)
{
    axt_app t;
    ax_window_desc d = axt_window_desc(300, 200, AX_DECORATIONS_NONE);
    publisher p;
    ax_thread th;
    ax_window_impl *w;
    const ax_hit_map *map;
    uint32_t torn = 0;
    float last_x = 0;
    uint32_t backwards = 0;
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(axt_app_create(&t, 0) == AX_OK);
    AX_REQUIRE(ax_window_create(t.app, &d, &p.window) == AX_OK);
    ax_atomic_init_u32(&p.done, 0);
    ax_atomic_init_u32(&p.failures, 0);
    w = ax__window_from_handle(p.window.h);
    AX_REQUIRE(w != NULL);
    AX_CHECK(ax__window_hit_map(w) == NULL); /* nothing published yet */

    AX_REQUIRE(ax_thread_create(&th, publish_many, &p) == AX_OK);
    /* The main thread reads concurrently, as it does on pointer events. */
    while (ax_atomic_load_u32(&p.done, AX_MO_ACQUIRE) == 0) {
        map = ax__window_hit_map(w);
        if (map != NULL) {
            uint32_t k;
            for (k = 1; k < map->count; ++k) {
                if (map->regions[k].x != map->regions[0].x) {
                    torn++;
                }
            }
            if (map->count != ((uint32_t)map->regions[0].x % AX_HIT_REGIONS_MAX) + 1u) {
                torn++;
            }
            if (map->regions[0].x < last_x) {
                backwards++;
            }
            last_x = map->regions[0].x;
        }
    }
    ax_thread_join(&th);
    map = ax__window_hit_map(w);
    AX_REQUIRE(map != NULL);
    AX_CHECK(map->regions[0].x == (float)PUBLISHES);
    AX_CHECK_EQ_U(torn, 0);
    AX_CHECK_EQ_U(backwards, 0);
    AX_CHECK_EQ_U(ax_atomic_load_u32(&p.failures, AX_MO_RELAXED), 0);
    AX_CHECK_RESULT(ax_window_destroy(p.window), AX_OK);
    AX_CHECK_EQ_U(axt_app_destroy(&t), 0);
}

AX_TEST(region_count_is_limited)
{
    axt_app t;
    ax_window_desc d = axt_window_desc(300, 200, AX_DECORATIONS_NONE);
    ax_window w;
    ax_hit_region regions[AX_HIT_REGIONS_MAX + 1];
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(axt_app_create(&t, 0) == AX_OK);
    AX_REQUIRE(ax_window_create(t.app, &d, &w) == AX_OK);
    memset(regions, 0, sizeof regions);
    AX_CHECK_RESULT(ax_window_set_hit_regions(w, regions, AX_HIT_REGIONS_MAX), AX_OK);
    AX_CHECK_RESULT(ax_window_set_hit_regions(w, regions, AX_HIT_REGIONS_MAX + 1),
                    AX_ERR_UNSUPPORTED);
    AX_CHECK_RESULT(ax_window_set_hit_regions(w, NULL, 0), AX_OK); /* clears the map */
    AX_CHECK_RESULT(ax_window_destroy(w), AX_OK);
    AX_CHECK_EQ_U(axt_app_destroy(&t), 0);
}

AX_TEST(a_reused_window_slot_ignores_the_old_map)
{
    axt_app t;
    ax_window_desc d = axt_window_desc(300, 200, AX_DECORATIONS_NONE);
    ax_window first;
    ax_window second;
    ax_hit_region r = {0, 0, 300, 32, AX_REGION_CAPTION};
    ax_window_impl *w;
    AXT_REQUIRE_COMPOSITOR();
    AX_REQUIRE(axt_app_create(&t, 1) == AX_OK); /* one slot: the second window reuses it */
    AX_REQUIRE(ax_window_create(t.app, &d, &first) == AX_OK);
    AX_REQUIRE(ax_window_set_hit_regions(first, &r, 1) == AX_OK);
    AX_REQUIRE(ax_window_destroy(first) == AX_OK);
    AX_REQUIRE(ax_window_create(t.app, &d, &second) == AX_OK);
    w = ax__window_from_handle(second.h);
    AX_REQUIRE(w != NULL);
    AX_CHECK(ax__window_hit_map(w) == NULL);
    AX_CHECK_RESULT(ax_window_set_hit_regions(first, &r, 1), AX_ERR_INVALID_HANDLE);
    AX_CHECK_RESULT(ax_window_destroy(second), AX_OK);
    AX_CHECK_EQ_U(axt_app_destroy(&t), 0);
}
