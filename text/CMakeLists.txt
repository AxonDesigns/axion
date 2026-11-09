# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# text: fonts, itemization, shaping, line layout and glyph rasterization (the document's "Text
# rendering"). Depends only on base, its three libraries built from source
# (cmake/AxionTextDeps.cmake) and the operating system's font APIs (fontconfig on Linux), never on
# the platform module.
#
# Headers: include/axion/text/ (internal SDK).

include(AxionTextDeps)
ax_add_text_deps()

# The bundled font set (decision 98), compiled in: text/fonts/, rebuilt by
# tools/fonts/subset_noto.py.
set(_ax_fonts
    "NotoSans[wdth,wght].ttf:ax__font_noto_sans"
    "NotoSans-Italic[wdth,wght].ttf:ax__font_noto_sans_italic")
set(_ax_font_sources "")
foreach(_pair IN LISTS _ax_fonts)
    string(REPLACE ":" ";" _pair "${_pair}")
    list(GET _pair 0 _file)
    list(GET _pair 1 _symbol)
    set(_out "${CMAKE_CURRENT_BINARY_DIR}/generated/${_symbol}.c")
    add_custom_command(OUTPUT "${_out}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fonts/${_file}"
                "-DOUTPUT=${_out}" "-DSYMBOL=${_symbol}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/fonts/embed_font.cmake"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fonts/${_file}"
                "${CMAKE_CURRENT_SOURCE_DIR}/fonts/embed_font.cmake"
        COMMENT "Embedding ${_file}"
        VERBATIM)
    list(APPEND _ax_font_sources "${_out}")
endforeach()

add_library(ax_text STATIC
    src/cache.c
    src/glyphs.c
    src/layout.c
    src/chars.c
    src/paragraph.c
    src/source_bundled.c
    src/system.c
    ${_ax_font_sources})
add_library(axion::text ALIAS ax_text)
target_include_directories(ax_text
    PUBLIC  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(ax_text PUBLIC ax_base PRIVATE ax_harfbuzz ax_freetype ax_unibreak)
ax_target_defaults(ax_text)

# The system's fonts: fontconfig on Linux, optional (decision 98). Without it, only the bundled
# set; headless hosts use the bundled set either way.
set(_ax_fontconfig_default OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(AX_FONTCONFIG QUIET IMPORTED_TARGET fontconfig)
        if(AX_FONTCONFIG_FOUND)
            set(_ax_fontconfig_default ON)
        endif()
    endif()
endif()
option(AX_TEXT_FONTCONFIG "text: the system's fonts through fontconfig (Linux)"
       ${_ax_fontconfig_default})
if(AX_TEXT_FONTCONFIG)
    if(NOT AX_FONTCONFIG_FOUND)
        message(FATAL_ERROR "AX_TEXT_FONTCONFIG is ON but pkg-config cannot find fontconfig")
    endif()
    target_sources(ax_text PRIVATE src/source_fontconfig.c)
    target_link_libraries(ax_text PRIVATE PkgConfig::AX_FONTCONFIG)
    target_compile_definitions(ax_text PUBLIC AX_TEXT_HAS_FONTCONFIG=1)
    message(STATUS "text/: fontconfig ${AX_FONTCONFIG_VERSION}")
else()
    target_sources(ax_text PRIVATE src/source_none.c)
    message(STATUS "text/: no system font source (bundled fonts only)")
endif()
