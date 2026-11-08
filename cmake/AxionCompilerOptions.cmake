# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Compiler settings shared by every AXION target.
#
# All language-specific flags are scoped to C with $<COMPILE_LANGUAGE:C>, so a target that later
# mixes in Objective-C or other platform glue does not receive -std=c11 or C-only warnings.

include_guard(GLOBAL)

if(MSVC AND NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(_AX_C_MSVC ON)
elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang" AND CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(_AX_C_MSVC ON)   # clang-cl takes MSVC-style flags
else()
    set(_AX_C_MSVC OFF)
endif()

if(_AX_C_MSVC)
    set(_AX_C_WARNINGS
        /W4 /WX
        /utf-8             # sources are UTF-8
        /Zc:preprocessor)  # standard-conforming preprocessor (__VA_ARGS__ handling)
else()
    set(_AX_C_WARNINGS
        -Wall -Wextra -Wpedantic -Werror
        -Wvla                  # VLAs are banned: optional in C11 and absent in MSVC
        -Wshadow
        -Wstrict-prototypes
        -Wmissing-prototypes)  # every external function is declared in a header first
endif()

# ax_target_defaults(<target>)
#   Strict C11, warnings as errors, hidden symbols by default and the sanitizers in AX_SANITIZE.
function(ax_target_defaults target)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        C_VISIBILITY_PRESET hidden)

    foreach(flag IN LISTS _AX_C_WARNINGS)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:C>:${flag}>")
    endforeach()

    # Floating point is deterministic across platforms (decision 81): no contraction of a multiply
    # and an add into a fused multiply-add, which GCC and Clang do by default on targets that
    # have FMA (arm64) and would change the last bits of the Render IR and of what the software
    # renderer draws. MSVC's /fp:precise (its default, stated explicitly) does not contract since
    # Visual Studio 2022; /fp:contract and /fp:fast are never used.
    if(_AX_C_MSVC)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:C>:/fp:precise>")
    else()
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:C>:-ffp-contract=off>")
    endif()

    if(AX_SANITIZE)
        if(_AX_C_MSVC)
            if(NOT AX_SANITIZE STREQUAL "address")
                message(FATAL_ERROR "MSVC supports only AX_SANITIZE=address (got '${AX_SANITIZE}')")
            endif()
            target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:C>:/fsanitize=address>")
        else()
            list(JOIN AX_SANITIZE "," _list)
            target_compile_options(${target} PRIVATE
                "$<$<COMPILE_LANGUAGE:C>:-fsanitize=${_list}>"
                "$<$<COMPILE_LANGUAGE:C>:-fno-sanitize-recover=all>"
                "$<$<COMPILE_LANGUAGE:C>:-fno-omit-frame-pointer>")
            target_link_options(${target} PRIVATE "-fsanitize=${_list}")
        endif()
    endif()
endfunction()
