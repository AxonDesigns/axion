# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# core.includes.only_the_render_ir: core reads the Render IR (ax_render_ir.h) and its mailbox
# (ax_ir_mailbox.h), never the renderer interface (ax_renderer.h, which pulls in the platform
# module) nor any platform or gpu header: the scene and the renderers only meet in the IR
# (decision 76). Run with -DCORE_DIR=<core/>.

file(GLOB_RECURSE _files "${CORE_DIR}/*.c" "${CORE_DIR}/*.h")
set(_allowed "axion/render/ax_render_ir.h" "axion/render/ax_ir_mailbox.h")
set(_bad "")
foreach(_f IN LISTS _files)
    file(STRINGS "${_f}" _lines REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]axion/(render|platform|gpu)/")
    foreach(_l IN LISTS _lines)
        string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"].*" "\\1" _h "${_l}")
        if(NOT _h IN_LIST _allowed)
            list(APPEND _bad "${_f}: ${_h}")
        endif()
    endforeach()
endforeach()
if(_bad)
    list(JOIN _bad "\n  " _msg)
    message(FATAL_ERROR "core includes headers outside the Render IR:\n  ${_msg}")
endif()
list(LENGTH _files _n)
message(STATUS "core: ${_n} files include only the Render IR and its mailbox")
