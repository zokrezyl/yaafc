# 3rdparty-fetch.cmake
#
# For picomesh the "fetch" downloads a prebuilt per-lib tarball published by
# .github/workflows/build-3rdparty-<lib>.yml and attached to the
# `lib-<lib>-<version>` GitHub release, then extracts it into
# ${CMAKE_BINARY_DIR}/3rdparty/<lib>/ so the per-lib stubs (libs/*.cmake)
# get the lib/+include/ layout they expect.
#
# On a download miss — release not published for this version/platform
# yet, offline, or PICOMESH_3RDPARTY_FORCE_BUILD set — it falls back to a
# local from-source build via build-tools/3rdparty/<lib>/_build.sh, which
# produces an identically-shaped tarball under
# ${CMAKE_BINARY_DIR}/3rdparty-cache/.
#
# A `${CMAKE_BINARY_DIR}/3rdparty/<lib>/.fetched-<version>` stamp guards
# repeat work — re-fetch/extract happens only when the version pin moves.
#
# Env overrides:
#   PICOMESH_3RDPARTY_CACHE_DIR    default: ~/.cache/picomesh/3rdparty
#   PICOMESH_3RDPARTY_URL_BASE     default: github.com/zokrezyl/picomesh releases
#   PICOMESH_3RDPARTY_FORCE_BUILD  set to skip the download, build from source
#
# Public helpers:
#   picomesh_3rdparty_target_platform(OUT_VAR)
#   picomesh_3rdparty_fetch(LIB_NAME [DEST_VAR])

include_guard(GLOBAL)

if(NOT DEFINED PICOMESH_3RDPARTY_CACHE_DIR)
    if(DEFINED ENV{PICOMESH_3RDPARTY_CACHE_DIR})
        set(PICOMESH_3RDPARTY_CACHE_DIR "$ENV{PICOMESH_3RDPARTY_CACHE_DIR}")
    else()
        set(PICOMESH_3RDPARTY_CACHE_DIR "$ENV{HOME}/.cache/picomesh/3rdparty")
    endif()
endif()

# Release-asset download base. Prebuilt per-lib tarballs are published by
# .github/workflows/build-3rdparty-<lib>.yml and attached to the
# `lib-<lib>-<version>` GitHub release.
if(NOT DEFINED PICOMESH_3RDPARTY_URL_BASE)
    if(DEFINED ENV{PICOMESH_3RDPARTY_URL_BASE})
        set(PICOMESH_3RDPARTY_URL_BASE "$ENV{PICOMESH_3RDPARTY_URL_BASE}")
    else()
        set(PICOMESH_3RDPARTY_URL_BASE "https://github.com/zokrezyl/picomesh/releases/download")
    endif()
endif()

# Force a from-source build, skipping the prebuilt download — useful when
# iterating on a _build.sh recipe without bumping the version pin.
if(NOT DEFINED PICOMESH_3RDPARTY_FORCE_BUILD AND DEFINED ENV{PICOMESH_3RDPARTY_FORCE_BUILD})
    set(PICOMESH_3RDPARTY_FORCE_BUILD "$ENV{PICOMESH_3RDPARTY_FORCE_BUILD}")
endif()

set(PICOMESH_3RDPARTY_OUTPUT_DIR "${CMAKE_BINARY_DIR}/3rdparty")
file(MAKE_DIRECTORY "${PICOMESH_3RDPARTY_CACHE_DIR}")
file(MAKE_DIRECTORY "${PICOMESH_3RDPARTY_OUTPUT_DIR}")

function(picomesh_3rdparty_target_platform OUT_VAR)
    if(APPLE)
        # Honour an explicit single-arch request; otherwise follow the HOST
        # arch. The old code defaulted to arm64 whenever CMAKE_OSX_ARCHITECTURES
        # was unset, which fetched arm64 libs on an x86_64 (Intel/Rosetta) host
        # and failed the link with "found architecture 'arm64', required
        # architecture 'x86_64'".
        if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
            set(_P "macos-arm64")
        elseif(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
            set(_P "macos-x86_64")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            set(_P "macos-arm64")
        else()
            set(_P "macos-x86_64")
        endif()
    elseif(WIN32)
        set(_P "windows-x86_64")
    else()
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            set(_P "linux-aarch64")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64|riscv")
            set(_P "linux-riscv64")
        else()
            set(_P "linux-x86_64")
        endif()
    endif()
    set(${OUT_VAR} "${_P}" PARENT_SCOPE)
endfunction()

function(picomesh_3rdparty_fetch LIB_NAME)
    set(_LIB_DIR  "${PICOMESH_ROOT}/build-tools/3rdparty/${LIB_NAME}")
    set(_VER_FILE "${_LIB_DIR}/version")
    set(_BUILD_SH "${_LIB_DIR}/_build.sh")
    if(NOT EXISTS "${_VER_FILE}")
        message(FATAL_ERROR "3rdparty-fetch(${LIB_NAME}): missing ${_VER_FILE}")
    endif()
    if(NOT EXISTS "${_BUILD_SH}")
        message(FATAL_ERROR "3rdparty-fetch(${LIB_NAME}): missing ${_BUILD_SH}")
    endif()

    file(READ "${_VER_FILE}" _LIBVER)
    string(STRIP "${_LIBVER}" _LIBVER)

    picomesh_3rdparty_target_platform(_PLATFORM)

    set(_FILENAME "${LIB_NAME}-${_PLATFORM}-${_LIBVER}.tar.gz")
    set(_OUTPUT_DIR "${CMAKE_BINARY_DIR}/3rdparty-cache")
    set(_TARBALL "${_OUTPUT_DIR}/${_FILENAME}")
    set(_DEST "${PICOMESH_3RDPARTY_OUTPUT_DIR}/${LIB_NAME}")
    set(_STAMP "${_DEST}/.fetched-${_LIBVER}")

    if(NOT EXISTS "${_STAMP}")
        file(MAKE_DIRECTORY "${_OUTPUT_DIR}")
        if(NOT EXISTS "${_TARBALL}")
            set(_HAVE_TARBALL FALSE)

            # Prefer the prebuilt release asset; build from source only on
            # a miss (release not cut yet, unpublished platform, offline,
            # or PICOMESH_3RDPARTY_FORCE_BUILD set).
            if(NOT PICOMESH_3RDPARTY_FORCE_BUILD)
                set(_TAG "lib-${LIB_NAME}-${_LIBVER}")
                set(_URL "${PICOMESH_3RDPARTY_URL_BASE}/${_TAG}/${_FILENAME}")
                message(STATUS "3rdparty: fetching prebuilt ${_FILENAME}")
                file(DOWNLOAD "${_URL}" "${_TARBALL}.part"
                    STATUS _DL_STATUS TLS_VERIFY ON)
                list(GET _DL_STATUS 0 _DL_CODE)
                if(_DL_CODE EQUAL 0)
                    file(RENAME "${_TARBALL}.part" "${_TARBALL}")
                    set(_HAVE_TARBALL TRUE)
                    message(STATUS
                        "3rdparty: downloaded ${LIB_NAME} ${_LIBVER} (${_PLATFORM})")
                else()
                    file(REMOVE "${_TARBALL}.part")
                    list(GET _DL_STATUS 1 _DL_MSG)
                    # CMake's bundled libcurl can't always verify the GitHub TLS
                    # chain on Windows agents (missing CA bundle, or a Schannel
                    # revocation check against an unreachable endpoint). Retry
                    # with the system curl, which uses the OS trust store
                    # (Schannel on Windows) and --ssl-no-revoke to tolerate that.
                    # The prebuilt is published for every platform, so we fetch
                    # it rather than ever building the dependency on the host.
                    find_program(_PICOMESH_CURL curl)
                    if(_PICOMESH_CURL)
                        message(STATUS
                            "3rdparty: file(DOWNLOAD) failed (${_DL_MSG}); "
                            "retrying ${_FILENAME} with system curl")
                        execute_process(
                            COMMAND "${_PICOMESH_CURL}" -fLsS
                                --retry 5 --retry-delay 3 --ssl-no-revoke
                                -o "${_TARBALL}.part" "${_URL}"
                            RESULT_VARIABLE _CURL_RC)
                        if(_CURL_RC EQUAL 0 AND EXISTS "${_TARBALL}.part")
                            file(RENAME "${_TARBALL}.part" "${_TARBALL}")
                            set(_HAVE_TARBALL TRUE)
                            message(STATUS
                                "3rdparty: downloaded ${LIB_NAME} ${_LIBVER} "
                                "via curl (${_PLATFORM})")
                        else()
                            file(REMOVE "${_TARBALL}.part")
                        endif()
                    endif()
                    if(NOT _HAVE_TARBALL)
                        message(STATUS
                            "3rdparty: no prebuilt ${_FILENAME} (${_DL_MSG}) — "
                            "building from source")
                    endif()
                endif()
            endif()

            if(NOT _HAVE_TARBALL)
                message(STATUS "3rdparty: building ${LIB_NAME} @${_LIBVER} for ${_PLATFORM}")
                execute_process(
                    COMMAND ${CMAKE_COMMAND} -E env
                        "TARGET_PLATFORM=${_PLATFORM}"
                        "OUTPUT_DIR=${_OUTPUT_DIR}"
                        "CACHE_DIR=${PICOMESH_3RDPARTY_CACHE_DIR}"
                        bash "${_BUILD_SH}"
                    WORKING_DIRECTORY "${_LIB_DIR}"
                    RESULT_VARIABLE _BUILD_RC
                    OUTPUT_FILE "${_OUTPUT_DIR}/${LIB_NAME}-build.log"
                    ERROR_FILE  "${_OUTPUT_DIR}/${LIB_NAME}-build.err"
                )
                if(NOT _BUILD_RC EQUAL 0)
                    message(FATAL_ERROR
                        "3rdparty(${LIB_NAME}): _build.sh failed (rc=${_BUILD_RC}). "
                        "See ${_OUTPUT_DIR}/${LIB_NAME}-build.err")
                endif()
            endif()
        endif()

        if(EXISTS "${_DEST}")
            file(REMOVE_RECURSE "${_DEST}")
        endif()
        file(MAKE_DIRECTORY "${_DEST}")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf "${_TARBALL}"
            WORKING_DIRECTORY "${_DEST}"
            RESULT_VARIABLE _TAR_RC
        )
        if(NOT _TAR_RC EQUAL 0)
            message(FATAL_ERROR "3rdparty(${LIB_NAME}): extract failed")
        endif()
        file(WRITE "${_STAMP}" "${_LIBVER}\n")
        message(STATUS "3rdparty: ${LIB_NAME} ${_LIBVER} ready (${_PLATFORM})")
    endif()

    if(ARGC GREATER 1)
        set(${ARGV1} "${_DEST}" PARENT_SCOPE)
    endif()
    set(PICOMESH_3RDPARTY_${LIB_NAME}_VERSION "${_LIBVER}" CACHE INTERNAL "")
    set(PICOMESH_3RDPARTY_${LIB_NAME}_DIR     "${_DEST}"   CACHE INTERNAL "")
endfunction()
