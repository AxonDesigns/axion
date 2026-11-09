# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# text.includes.only_base: text depends on base, its three libraries and the operating system's
# font APIs only; never on core, the Render IR, the renderers, the platform module, gpu or the
# runtime (the owner's rule for text/). Run with -DTEXT_DIR=<text/>.

file(GLOB_RECURSE _files "${TEXT_DIR}/src/*.c" "${TEXT_DIR}/src/*.h" "${TEXT_DIR}/include/*.h")
set(_bad "")
foreach(_f IN LISTS _files)
    file(STRINGS "${_f}" _lines REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]axion/")
    foreach(_l IN LISTS _lines)
        string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"].*" "\\1" _h "${_l}")
        if(NOT _h MATCHES "^axion/(base|text)/" AND NOT _h MATCHES "^axion/ax_(allocator|types|config|error|handle|base)\\.h$")
            list(APPEND _bad "${_f}: ${_h}")
        endif()
    endforeach()
endforeach()
if(_bad)
    list(JOIN _bad "\n  " _msg)
    message(FATAL_ERROR "text includes headers outside base:\n  ${_msg}")
endif()
list(LENGTH _files _n)
message(STATUS "text: ${_n} files include only base and text")
