# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Development-only diagnostics. Built only with -DAX_BUILD_DIAG_TOOLS=ON, never installed, and
# never linked into an AXION library or example.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

# LD_PRELOAD library recording which threads read the Wayland socket (wl_read_trace.c).
add_library(ax_wl_read_trace MODULE wl_read_trace.c)
set_target_properties(ax_wl_read_trace PROPERTIES C_STANDARD 11 C_EXTENSIONS ON PREFIX "lib")
target_compile_options(ax_wl_read_trace PRIVATE -Wall -Wextra)
target_link_libraries(ax_wl_read_trace PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
