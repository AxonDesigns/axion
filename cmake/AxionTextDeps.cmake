# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Text's third-party libraries, built from source (the document's "Text rendering"): FreeType
# (font parsing and rasterization), HarfBuzz (shaping) and libunibreak (UAX #14 and #29). Each is
# pinned by version and SHA-256, downloaded once into AX_DEPS_DIR (cmake/AxionPrebuilt.cmake), and
# built by a target of ours rather than by its own build system, so it gets the same toolchain,
# sanitizers and determinism flags as the rest of AXION on every platform (MSVC and Emscripten
# included), and only the parts text uses.
#
#   ax_freetype  C. The modules for TrueType and CFF outlines (sfnt, truetype, cff, psaux,
#                pshinter, psnames), the smooth rasterizer and the autohinter (light hinting).
#                No zlib, bzip2, libpng, brotli or HarfBuzz integration (no WOFF, no PNG strikes).
#   ax_harfbuzz  C++ (the only C++ in AXION, decision 97): the amalgamated harfbuzz.cc with
#                HarfBuzz's own OpenType font functions; no FreeType, ICU, GLib, Core Text,
#                DirectWrite or Uniscribe integration. Used only through its C API.
#   ax_unibreak  C. Line, word and grapheme breaking.
#
# Third-party code keeps its own warnings (not AXION's -Werror) but gets the sanitizers in
# AX_SANITIZE and no floating-point contraction, so text is deterministic across platforms
# (decision 81).

include_guard(GLOBAL)
include(AxionPrebuilt)

set(AX_HARFBUZZ_VERSION "15.0.0")
set(AX_HARFBUZZ_SHA256 "968536e9abd782f07f7954044c0c4020199fcdd39b41d221ec42d2a3be4a44df")
set(AX_FREETYPE_VERSION "2.15.0")
set(AX_FREETYPE_SHA256 "93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5")
set(AX_UNIBREAK_VERSION "7.0")
set(AX_UNIBREAK_SHA256 "8c9a6e121736cd0d5c890ae3ae96f3f4010a19aa040f1dbded833a62a87717d3")

# Determinism and sanitizers for a third-party target, in C and C++.
function(_ax_third_party_defaults target)
    set_target_properties(${target} PROPERTIES
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        # /bigobj: the amalgamated harfbuzz.cc has more sections than the default COFF limit.
        target_compile_options(${target} PRIVATE /fp:precise /utf-8 /W0 /bigobj
            "$<$<COMPILE_LANGUAGE:CXX>:/EHs-c->" "$<$<COMPILE_LANGUAGE:CXX>:/GR->")
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS _HAS_EXCEPTIONS=0)
    else()
        target_compile_options(${target} PRIVATE -ffp-contract=off -w
            "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>" "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>"
            "$<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>")
    endif()
    if(AX_SANITIZE)
        if(MSVC)
            target_compile_options(${target} PRIVATE /fsanitize=address)
        else()
            list(JOIN AX_SANITIZE "," _list)
            target_compile_options(${target} PRIVATE "-fsanitize=${_list}"
                "-fno-sanitize-recover=all" "-fno-omit-frame-pointer")
            # HarfBuzz is C++ without RTTI: UBSan's vptr check needs RTTI.
            if("undefined" IN_LIST AX_SANITIZE)
                target_compile_options(${target} PRIVATE
                    "$<$<COMPILE_LANGUAGE:CXX>:-fno-sanitize=vptr>")
            endif()
        endif()
    endif()
endfunction()

# ax_add_text_deps()
#   Defines ax_freetype, ax_harfbuzz and ax_unibreak (once).
function(ax_add_text_deps)
    if(TARGET ax_harfbuzz)
        return()
    endif()

    # ---- FreeType ------------------------------------------------------------------------------
    ax_fetch_prebuilt(_ft "freetype-${AX_FREETYPE_VERSION}"
        "https://download.savannah.gnu.org/releases/freetype/freetype-${AX_FREETYPE_VERSION}.tar.xz"
        "${AX_FREETYPE_SHA256}"
        # Mirrors, for when Savannah answers with an HTTP error (seen twice in CI, 2026-11-08/09).
        "https://download-mirror.savannah.gnu.org/releases/freetype/freetype-${AX_FREETYPE_VERSION}.tar.xz"
        "https://downloads.sourceforge.net/project/freetype/freetype2/${AX_FREETYPE_VERSION}/freetype-${AX_FREETYPE_VERSION}.tar.xz")
    add_library(ax_freetype STATIC
        "${_ft}/src/base/ftsystem.c"
        "${_ft}/src/base/ftinit.c"
        "${_ft}/src/base/ftdebug.c"
        "${_ft}/src/base/ftbase.c"
        "${_ft}/src/base/ftbbox.c"
        "${_ft}/src/base/ftbitmap.c"
        "${_ft}/src/base/ftglyph.c"
        "${_ft}/src/base/ftmm.c"
        "${_ft}/src/base/ftsynth.c"
        "${_ft}/src/autofit/autofit.c"
        "${_ft}/src/cff/cff.c"
        "${_ft}/src/psaux/psaux.c"
        "${_ft}/src/pshinter/pshinter.c"
        "${_ft}/src/psnames/psnames.c"
        "${_ft}/src/sfnt/sfnt.c"
        "${_ft}/src/smooth/smooth.c"
        "${_ft}/src/truetype/truetype.c")
    # ax_ftmodule.h lists the modules above, ax_ftoption.h turns off gzip, LZW and OT-SVG
    # (FreeType's FT_CONFIG_MODULES_H and FT_CONFIG_OPTIONS_H hooks).
    target_include_directories(ax_freetype SYSTEM
        PUBLIC "${_ft}/include"
        PRIVATE "${PROJECT_SOURCE_DIR}/text/third_party/freetype")
    target_compile_definitions(ax_freetype PRIVATE FT2_BUILD_LIBRARY
        "FT_CONFIG_MODULES_H=<ax_ftmodule.h>" "FT_CONFIG_OPTIONS_H=<ax_ftoption.h>")
    _ax_third_party_defaults(ax_freetype)

    # ---- HarfBuzz ------------------------------------------------------------------------------
    ax_fetch_prebuilt(_hb "harfbuzz-${AX_HARFBUZZ_VERSION}"
        "https://github.com/harfbuzz/harfbuzz/releases/download/${AX_HARFBUZZ_VERSION}/harfbuzz-${AX_HARFBUZZ_VERSION}.tar.xz"
        "${AX_HARFBUZZ_SHA256}")
    add_library(ax_harfbuzz STATIC "${_hb}/src/harfbuzz.cc")
    target_include_directories(ax_harfbuzz SYSTEM PUBLIC "${_hb}/src")
    set_target_properties(ax_harfbuzz PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    # Without threads (the single-threaded web build) HarfBuzz needs no atomics or mutexes.
    if(EMSCRIPTEN AND NOT AX_WEB_THREADS)
        target_compile_definitions(ax_harfbuzz PRIVATE HB_NO_MT)
    endif()
    _ax_third_party_defaults(ax_harfbuzz)
    # A static C++ library linked into C programs: the C++ runtime comes with it.
    set_target_properties(ax_harfbuzz PROPERTIES LINKER_LANGUAGE CXX)
    if(NOT MSVC AND NOT APPLE AND NOT EMSCRIPTEN)
        # -fno-exceptions -fno-rtti: only the C++ runtime's support functions, never libstdc++'s
        # library (HarfBuzz uses no standard library containers or iostreams).
        target_link_libraries(ax_harfbuzz PUBLIC m)
    endif()

    # ---- libunibreak ---------------------------------------------------------------------------
    ax_fetch_prebuilt(_ub "libunibreak-${AX_UNIBREAK_VERSION}"
        "https://github.com/adah1972/libunibreak/releases/download/libunibreak_7_0/libunibreak-${AX_UNIBREAK_VERSION}.tar.gz"
        "${AX_UNIBREAK_SHA256}")
    add_library(ax_unibreak STATIC
        "${_ub}/src/linebreak.c"
        "${_ub}/src/linebreakdata.c"
        "${_ub}/src/linebreakdef.c"
        "${_ub}/src/wordbreak.c"
        "${_ub}/src/graphemebreak.c"
        "${_ub}/src/eastasianwidthdef.c"
        "${_ub}/src/emojidef.c"
        "${_ub}/src/unibreakbase.c"
        "${_ub}/src/unibreakdef.c")
    target_include_directories(ax_unibreak SYSTEM PUBLIC "${_ub}/src")
    _ax_third_party_defaults(ax_unibreak)

    set(AX_TEXT_DEP_DIRS "${_ft};${_hb};${_ub}" PARENT_SCOPE)
endfunction()
