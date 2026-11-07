# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# base: allocators, arenas, containers, handles, atomics, threads and 2D math. Depends on no other AXION
# module and no third-party library; only the C standard library and the OS thread primitives.
#
# Headers:
#   include/axion/        stable C ABI (bindings and apps may include these)
#   include/axion/base/   internal SDK shared between AXION modules (never part of the ABI)

# Threads: Win32 on Windows; on the web, pthreads only when built with -pthread, otherwise the
# single-threaded implementation; pthreads everywhere else.
if(WIN32)
    set(_ax_thread_src src/thread_win32.c)
elseif(EMSCRIPTEN AND NOT AX_WEB_THREADS)
    set(_ax_thread_src src/thread_single.c)
else()
    set(_ax_thread_src src/thread_posix.c)
endif()

add_library(ax_base STATIC
    src/allocator.c
    src/arena.c
    src/array.c
    src/error.c
    src/jobs.c
    src/math.c
    src/pool.c
    src/thread_common.c
    src/tracking_allocator.c
    src/types.c
    ${_ax_thread_src})
add_library(axion::base ALIAS ax_base)

target_include_directories(ax_base
    PUBLIC  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

ax_target_defaults(ax_base)

if(EMSCRIPTEN)
    if(AX_WEB_THREADS)
        # Everything linking base must also be built with -pthread on Emscripten.
        target_compile_options(ax_base PUBLIC -pthread)
        target_link_options(ax_base PUBLIC -pthread)
    endif()
elseif(NOT WIN32)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(ax_base PUBLIC Threads::Threads)
endif()

# ax_math.c uses the C math library, which is separate from libc on Linux and the web.
if(UNIX AND NOT APPLE)
    target_link_libraries(ax_base PUBLIC m)
endif()
