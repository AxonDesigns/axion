# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# The WebGPU implementation gpu/ links: Dawn (primary) or wgpu-native (alternative kept compiling
# in CI). Both are prebuilt archives pinned in gpu/third_party/webgpu_deps.cmake.
#
# Defines the INTERFACE target ax_webgpu: the implementation's headers (as system headers, so
# AXION's warnings do not apply to them), its libraries, the C++ runtime a static Dawn needs, and
# one of the compile definitions AX_WEBGPU_DAWN or AX_WEBGPU_WGPU_NATIVE.
#
# AX_DAWN_ROOT / AX_WGPU_NATIVE_ROOT point at an already unpacked package instead (offline builds,
# or trying another version locally); the pinned checksum is then not checked.

include_guard(GLOBAL)
include(AxionPrebuilt)
include("${PROJECT_SOURCE_DIR}/gpu/third_party/webgpu_deps.cmake")

set(AX_DAWN_ROOT "" CACHE PATH "Use this unpacked Dawn package instead of the pinned download")
set(AX_WGPU_NATIVE_ROOT "" CACHE PATH
    "Use this unpacked wgpu-native package instead of the pinned download")

# The pin key: <system>_<processor>, normalised the way the manifest spells it.
function(_ax_webgpu_target_key out_var)
    set(_proc "${CMAKE_SYSTEM_PROCESSOR}")
    if(_proc MATCHES "^(AMD64|amd64|x64)$")
        set(_proc x86_64)
    elseif(_proc MATCHES "^(aarch64|ARM64)$")
        set(_proc arm64)
    endif()
    if(APPLE AND CMAKE_OSX_ARCHITECTURES)
        list(LENGTH CMAKE_OSX_ARCHITECTURES _n)
        if(NOT _n EQUAL 1)
            message(FATAL_ERROR "gpu/: universal macOS builds are not supported yet "
                                "(CMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES})")
        endif()
        set(_proc "${CMAKE_OSX_ARCHITECTURES}")
    endif()
    set(${out_var} "${CMAKE_SYSTEM_NAME}_${_proc}" PARENT_SCOPE)
endfunction()

function(_ax_webgpu_package out_var prefix name version)
    if(${prefix}_ROOT)
        set(${out_var} "${${prefix}_ROOT}" PARENT_SCOPE)
        return()
    endif()
    _ax_webgpu_target_key(_key)
    if(NOT DEFINED ${prefix}_URL_${_key})
        message(FATAL_ERROR "gpu/: no pinned ${name} package for ${_key}. Set ${prefix}_ROOT to an "
                            "unpacked package, or turn AX_BUILD_GPU off.")
    endif()
    ax_fetch_prebuilt(_dir "${name}-${version}-${_key}" "${${prefix}_URL_${_key}}"
                      "${${prefix}_SHA256_${_key}}")
    set(${out_var} "${_dir}" PARENT_SCOPE)
endfunction()

set(_ax_webgpu_impls dawn wgpu-native)
set(AX_WEBGPU "dawn" CACHE STRING "WebGPU implementation for gpu/: dawn or wgpu-native")
set_property(CACHE AX_WEBGPU PROPERTY STRINGS ${_ax_webgpu_impls})
if(NOT AX_WEBGPU IN_LIST _ax_webgpu_impls)
    message(FATAL_ERROR "AX_WEBGPU must be one of: ${_ax_webgpu_impls} (got '${AX_WEBGPU}')")
endif()

find_package(Threads REQUIRED)
add_library(ax_webgpu INTERFACE)

if(AX_WEBGPU STREQUAL "dawn")
    _ax_webgpu_package(_root AX_DAWN dawn "${AX_DAWN_VERSION}")
    # The Linux package installs to lib64/, the Apple ones to lib/.
    file(GLOB _cfg "${_root}/lib*/cmake/Dawn/DawnConfig.cmake")
    if(NOT _cfg)
        message(FATAL_ERROR "gpu/: ${_root} does not look like a Dawn package")
    endif()
    list(GET _cfg 0 _cfg)
    get_filename_component(_cfg_dir "${_cfg}" DIRECTORY)
    find_package(Dawn CONFIG REQUIRED PATHS "${_cfg_dir}" NO_DEFAULT_PATH)
    target_link_libraries(ax_webgpu INTERFACE dawn::webgpu_dawn)
    target_compile_definitions(ax_webgpu INTERFACE AX_WEBGPU_DAWN=1)
    # Dawn is C++ built as a static library: whoever links it needs the C++ runtime it was built
    # against (libstdc++ for the Linux package, libc++ for the Apple ones). The C driver links
    # the rest of the program, so the runtime is named explicitly.
    if(APPLE)
        target_link_libraries(ax_webgpu INTERFACE c++)
    else()
        target_link_libraries(ax_webgpu INTERFACE stdc++ m)
    endif()
    # Mark the package headers as system headers (imported targets usually are; be explicit).
    get_target_property(_inc dawn::dawn_public_config INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(ax_webgpu SYSTEM INTERFACE ${_inc})
    set(AX_WEBGPU_DESCRIPTION "Dawn ${AX_DAWN_VERSION} (${_root})")
else()
    _ax_webgpu_package(_root AX_WGPU_NATIVE wgpu-native "${AX_WGPU_NATIVE_VERSION}")
    if(NOT EXISTS "${_root}/lib/libwgpu_native.a" OR NOT EXISTS "${_root}/include/webgpu/webgpu.h")
        message(FATAL_ERROR "gpu/: ${_root} does not look like a wgpu-native package")
    endif()
    add_library(ax_wgpu_native STATIC IMPORTED)
    set_target_properties(ax_wgpu_native PROPERTIES
        IMPORTED_LOCATION "${_root}/lib/libwgpu_native.a")
    target_include_directories(ax_webgpu SYSTEM INTERFACE "${_root}/include")
    # A Rust static library: it needs libm, libdl and threads from the C side.
    target_link_libraries(ax_webgpu INTERFACE ax_wgpu_native m ${CMAKE_DL_LIBS} Threads::Threads)
    target_compile_definitions(ax_webgpu INTERFACE AX_WEBGPU_WGPU_NATIVE=1)
    set(AX_WEBGPU_DESCRIPTION "wgpu-native ${AX_WGPU_NATIVE_VERSION} (${_root})")
endif()

message(STATUS "gpu/: WebGPU implementation: ${AX_WEBGPU_DESCRIPTION}")
