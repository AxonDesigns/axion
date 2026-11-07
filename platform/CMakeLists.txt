# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# platform: apps, windows, surfaces, input and events over the OS windowing system. Depends only
# on base (and on the windowing system's own client libraries).
#
# Headers:
#   include/axion/            stable C ABI
#   include/axion/platform/   internal SDK shared between AXION modules (gpu/ uses it next)
#
# Backends: Wayland only so far. src/common/ holds everything that does not depend on the
# windowing system and talks to the backend through an internal vtable.

set(_ax_platform_common
    src/common/app.c
    src/common/event_queue.c
    src/common/frame_clock.c
    src/common/hit_test.c
    src/common/window.c)

set(_ax_platform_wayland
    src/wayland/wl_app.c
    src/wayland/wl_pointer.c
    src/wayland/wl_present.c
    src/wayland/wl_window.c)

add_library(ax_platform STATIC ${_ax_platform_common} ${_ax_platform_wayland})
add_library(axion::platform ALIAS ax_platform)

target_include_directories(ax_platform
    PUBLIC  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(ax_platform PUBLIC ax_base)
ax_target_defaults(ax_platform)

# ---- Wayland ----------------------------------------------------------------------------------

find_package(PkgConfig REQUIRED)
# 1.22: wl_surface.preferred_buffer_scale (wl_compositor version 6).
pkg_check_modules(AX_WAYLAND_CLIENT REQUIRED IMPORTED_TARGET "wayland-client>=1.22")
# Cursor images where the compositor lacks wp-cursor-shape-v1 (decision 33).
pkg_check_modules(AX_WAYLAND_CURSOR REQUIRED IMPORTED_TARGET wayland-cursor)
find_program(AX_WAYLAND_SCANNER NAMES wayland-scanner REQUIRED)

# Client header and glue code for each vendored protocol (src/wayland/protocols/README.md).
set(_ax_protocols
    xdg-shell
    xdg-decoration-unstable-v1
    presentation-time
    viewporter
    fractional-scale-v1
    cursor-shape-v1
    tablet-v2)
set(_ax_proto_dir "${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols")
file(MAKE_DIRECTORY "${_ax_proto_dir}")
set(_ax_proto_sources "")
foreach(_p IN LISTS _ax_protocols)
    set(_xml "${CMAKE_CURRENT_SOURCE_DIR}/src/wayland/protocols/${_p}.xml")
    set(_h "${_ax_proto_dir}/${_p}-client-protocol.h")
    set(_c "${_ax_proto_dir}/${_p}-protocol.c")
    add_custom_command(
        OUTPUT "${_h}"
        COMMAND "${AX_WAYLAND_SCANNER}" client-header "${_xml}" "${_h}"
        DEPENDS "${_xml}"
        COMMENT "wayland-scanner client-header ${_p}"
        VERBATIM)
    add_custom_command(
        OUTPUT "${_c}"
        COMMAND "${AX_WAYLAND_SCANNER}" private-code "${_xml}" "${_c}"
        DEPENDS "${_xml}"
        COMMENT "wayland-scanner private-code ${_p}"
        VERBATIM)
    list(APPEND _ax_proto_sources "${_h}" "${_c}")
endforeach()

# Generated glue is not AXION code: built in its own object library without -Werror, so a scanner
# version that emits a new warning cannot break the build.
add_library(ax_wayland_protocols OBJECT ${_ax_proto_sources})
target_link_libraries(ax_wayland_protocols PRIVATE PkgConfig::AX_WAYLAND_CLIENT)
set_target_properties(ax_wayland_protocols PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    C_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON)

target_sources(ax_platform PRIVATE $<TARGET_OBJECTS:ax_wayland_protocols>)
# Generated headers are included as system headers; AXION's own code stays fully checked.
target_include_directories(ax_platform SYSTEM PRIVATE "${_ax_proto_dir}")
add_dependencies(ax_platform ax_wayland_protocols)
target_link_libraries(ax_platform
    PUBLIC  PkgConfig::AX_WAYLAND_CLIENT
    PRIVATE PkgConfig::AX_WAYLAND_CURSOR m)
