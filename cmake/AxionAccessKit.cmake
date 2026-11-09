# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# AccessKit (decision 108): the platform accessibility adapters, through accesskit-c's C API, as
# the official prebuilt release (like Dawn, decision 43): pinned by version and SHA-256, unpacked
# into .deps/ by ax_fetch_prebuilt. No Rust toolchain is needed to build AXION.
#
#   AX_ACCESSKIT  AUTO (default): ON where the release has a binary for the target and the
#                 platform module builds (Linux x86_64 for now; the other desktop bridges are M3),
#                 except under ThreadSanitizer (the Rust code is not instrumented);
#                 ON: required (a configure error where it cannot be used); OFF: never.
#   AX_ACCESSKIT_ROOT  an unpacked accesskit-c release to use instead of downloading (offline).
#
# Without it everything builds and works the same, the accessibility tree and the headless
# queries included: platform's adapter then reports AX_ERR_UNSUPPORTED. The web has no AccessKit
# adapter: its bridge (a hidden ARIA DOM) will be AXION's own.
#
# Defines the imported target ax_accesskit and AX_HAVE_ACCESSKIT (ON or OFF) in the parent scope.
#
# The release archive holds every platform's libraries (68 MB); the binary linked here is
# lib/<os>/<arch>/static/libaccesskit.a. The licenses of every crate linked into it are checked
# by tools/licenses/accesskit_licenses.py (run it at every update; it fails on a new license) and
# listed in NOTICE.

include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/AxionPrebuilt.cmake")

set(AX_ACCESSKIT_VERSION "0.23.2")
set(AX_ACCESSKIT_URL
    "https://github.com/AccessKit/accesskit-c/releases/download/${AX_ACCESSKIT_VERSION}/accesskit-c-${AX_ACCESSKIT_VERSION}.zip")
set(AX_ACCESSKIT_SHA256 "c7fa84f8f34fca909f662375737a54f98b17cf9eae51fffd94ab3d9f662e3681")

set(AX_ACCESSKIT "AUTO" CACHE STRING "Platform accessibility through AccessKit: AUTO, ON or OFF")
set_property(CACHE AX_ACCESSKIT PROPERTY STRINGS AUTO ON OFF)
set(AX_ACCESSKIT_ROOT "" CACHE PATH "An unpacked accesskit-c release (instead of downloading it)")

function(ax_add_accesskit)
    set(AX_HAVE_ACCESSKIT OFF PARENT_SCOPE)
    if(AX_ACCESSKIT STREQUAL "OFF")
        return()
    endif()
    # Where the release has a static library for this target.
    set(_os "")
    set(_arch "")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
        set(_os "linux")
        set(_arch "x86_64")
    endif()
    set(_why "")
    if(_os STREQUAL "")
        set(_why "no AccessKit bridge for ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR} yet")
    elseif(NOT AX_BUILD_PLATFORM)
        set(_why "the platform module is off")
    elseif("thread" IN_LIST AX_SANITIZE AND AX_ACCESSKIT STREQUAL "AUTO")
        set(_why "ThreadSanitizer cannot see the Rust code's synchronization")
    endif()
    if(NOT _why STREQUAL "")
        if(AX_ACCESSKIT STREQUAL "ON")
            message(FATAL_ERROR "AX_ACCESSKIT=ON: ${_why}")
        endif()
        message(STATUS "AccessKit: off (${_why})")
        return()
    endif()

    if(AX_ACCESSKIT_ROOT)
        set(_root "${AX_ACCESSKIT_ROOT}")
    else()
        ax_fetch_prebuilt(_root "accesskit-c-${AX_ACCESSKIT_VERSION}" "${AX_ACCESSKIT_URL}"
                          "${AX_ACCESSKIT_SHA256}")
    endif()
    set(_lib "${_root}/lib/${_os}/${_arch}/static/libaccesskit.a")
    if(NOT EXISTS "${_lib}" OR NOT EXISTS "${_root}/include/accesskit.h")
        message(FATAL_ERROR "AccessKit: ${_root} is not an accesskit-c release (no ${_lib})")
    endif()
    add_library(ax_accesskit STATIC IMPORTED GLOBAL)
    set_target_properties(ax_accesskit PROPERTIES IMPORTED_LOCATION "${_lib}")
    # Its header as a system header: AXION's warnings stay on its own code.
    target_include_directories(ax_accesskit SYSTEM INTERFACE "${_root}/include")
    # What the release's own CMake package links on Linux; threads and dlopen live in glibc.
    find_package(Threads REQUIRED)
    target_link_libraries(ax_accesskit INTERFACE Threads::Threads ${CMAKE_DL_LIBS} m)
    message(STATUS "AccessKit: ${AX_ACCESSKIT_VERSION} (${_lib})")
    set(AX_HAVE_ACCESSKIT ON PARENT_SCOPE)
endfunction()
