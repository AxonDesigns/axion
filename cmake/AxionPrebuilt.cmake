# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Downloads and unpacks pinned dependencies (prebuilt packages and source archives), verifying
# each archive's SHA-256 first.

include_guard(GLOBAL)

# Shared by every build tree, so switching presets does not download the same archive again.
set(AX_DEPS_DIR "${PROJECT_SOURCE_DIR}/.deps" CACHE PATH
    "Where pinned prebuilt dependencies are downloaded and unpacked")

# ax_fetch_prebuilt(<out_var> <name> <url> <sha256> [<mirror_url>...])
#   Makes <AX_DEPS_DIR>/<name> hold the unpacked archive and sets <out_var> to that directory.
#   The URLs are tried in order, each twice, until one gives the pinned archive: the checksum makes
#   any mirror as trustworthy as the first URL, and one failing host (an HTTP error from a busy
#   server) does not fail the configure.
#   <name> must change whenever the pin does (include the version). An archive with a single
#   top-level directory is unpacked without it. The checksum is recorded once unpacking succeeds,
#   so an interrupted download or extraction is redone on the next configure, and a directory
#   whose recorded checksum differs from the pin is replaced.
function(ax_fetch_prebuilt out_var name url sha256)
    set(_dir "${AX_DEPS_DIR}/${name}")
    set(_stamp "${_dir}/.ax-sha256")

    file(MAKE_DIRECTORY "${AX_DEPS_DIR}/.locks")
    # Two build trees configured at once must not unpack into the same directory.
    file(LOCK "${AX_DEPS_DIR}/.locks/${name}" GUARD FUNCTION TIMEOUT 1200)

    if(EXISTS "${_stamp}")
        file(READ "${_stamp}" _recorded)
        string(STRIP "${_recorded}" _recorded)
        if(_recorded STREQUAL sha256)
            set(${out_var} "${_dir}" PARENT_SCOPE)
            return()
        endif()
    endif()

    get_filename_component(_archive_name "${url}" NAME)
    set(_archive "${AX_DEPS_DIR}/${_archive_name}")
    set(_tmp "${AX_DEPS_DIR}/${name}.tmp")

    set(_errors "")
    set(_done FALSE)
    foreach(_url IN ITEMS "${url}" ${ARGN})
        foreach(_attempt RANGE 1 2)
            message(STATUS "Downloading ${_archive_name} from ${_url}")
            # The hash is checked after the download: with EXPECTED_HASH, a failed download is a
            # fatal error before STATUS can be read, and no other URL would be tried.
            file(DOWNLOAD "${_url}" "${_archive}" TLS_VERIFY ON STATUS _status)
            list(GET _status 0 _code)
            if(_code EQUAL 0)
                file(SHA256 "${_archive}" _got)
                if(_got STREQUAL sha256)
                    set(_done TRUE)
                    break()
                endif()
                set(_msg "SHA-256 ${_got}, expected ${sha256}")
            else()
                list(GET _status 1 _msg)
            endif()
            file(REMOVE "${_archive}")
            string(APPEND _errors "\n  ${_url} (attempt ${_attempt}): ${_msg}")
        endforeach()
        if(_done)
            break()
        endif()
    endforeach()
    if(NOT _done)
        message(FATAL_ERROR "Downloading ${_archive_name} failed:${_errors}")
    endif()

    file(REMOVE_RECURSE "${_tmp}" "${_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_tmp}")
    file(GLOB _entries LIST_DIRECTORIES true "${_tmp}/*")
    list(LENGTH _entries _count)
    if(_count EQUAL 1 AND IS_DIRECTORY "${_entries}")
        file(RENAME "${_entries}" "${_dir}")
        file(REMOVE_RECURSE "${_tmp}")
    else()
        file(RENAME "${_tmp}" "${_dir}")
    endif()
    file(REMOVE "${_archive}")
    file(WRITE "${_stamp}" "${sha256}\n")

    set(${out_var} "${_dir}" PARENT_SCOPE)
endfunction()
