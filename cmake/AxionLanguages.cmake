# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Per-platform languages beyond C.
#
# Principle 7 allows thin glue in the language of an API that C cannot reach comfortably, kept
# behind a C interface: Objective-C for AppKit and UIKit, Kotlin or Java for Android. The project
# declares only C today. When a backend that needs one of these lands, enable it here, guarded
# by platform, so other targets never require that toolchain:
#
#   if(APPLE)
#       enable_language(OBJC)          # platform/appkit, platform/uikit
#   endif()
#
# Android's Kotlin/Java sources are built by Gradle, not CMake; the CMake side only builds the
# C/JNI library that Gradle packages.
#
# Strict C11 settings in AxionCompilerOptions.cmake are scoped to $<COMPILE_LANGUAGE:C>, so glue
# sources in another language keep their own standard and warning settings.

include_guard(GLOBAL)

function(ax_enable_platform_languages)
    # Nothing to enable until the first platform backend (M0, later step).
endfunction()
