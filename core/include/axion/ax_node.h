/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stable ABI: nodes of the retained tree.
 *
 * A node is a typed 64-bit handle (ax_handle.h). The app thread creates nodes in transactions and
 * the scene thread owns the tree they form; a node destroyed (with its subtree) makes every copy of
 * its handle stale, and a stale handle fails with AX_ERR_INVALID_HANDLE wherever it is used.
 */
#ifndef AX_NODE_H
#define AX_NODE_H

#include <stdint.h>

#include <axion/ax_config.h>
#include <axion/ax_handle.h>

AX_EXTERN_C_BEGIN

AX_DECLARE_HANDLE(ax_node);

/* What a node is. Values are fixed forever; new kinds are appended. */
typedef enum ax_node_kind {
    AX_NODE_VIEW = 1 /* a box: layout, background, corners, opacity, transform, children */
} ax_node_kind;

AX_EXTERN_C_END

#endif /* AX_NODE_H */
