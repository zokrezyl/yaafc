# 3rdparty-fetch.cmake
#
# For yaafc the "fetch" is actually a *local build* — we invoke
# build-tools/3rdparty/<lib>/_build.sh during cmake-configure, which
# downloads upstream source, compiles, and produces a tarball under
# ${CMAKE_BINARY_DIR}/3rdparty-cache/. We then extract it into
# ${CMAKE_BINARY_DIR}/3rdparty/<lib>/ so the per-lib stubs (libs/*.cmake)
# get the same lib/+include/ layout they would from a release tarball.
#
# A `${CMAKE_BINARY_DIR}/3rdparty/<lib>/.fetched-<version>` stamp guards
# repeat work — re-extraction happens only when the version pin moves.
#
# Single env override:
#   YAAFC_3RDPARTY_CACHE_DIR   default: ~/.cache/yaafc/3rdparty
#
# Public helpers:
#   yaafc_3rdparty_target_platform(OUT_VAR)
#   yaafc_3rdparty_fetch(LIB_NAME [DEST_VAR])

include_guard(GLOBAL)

if(NOT DEFINED YAAFC_3RDPARTY_CACHE_DIR)
    if(DEFINED ENV{YAAFC_3RDPARTY_CACHE_DIR})
        set(YAAFC_3RDPARTY_CACHE_DIR "$ENV{YAAFC_3RDPARTY_CACHE_DIR}")
    else()
        set(YAAFC_3RDPARTY_CACHE_DIR "$ENV{HOME}/.cache/yaafc/3rdparty")
    endif()
endif()

set(YAAFC_3RDPARTY_OUTPUT_DIR "${CMAKE_BINARY_DIR}/3rdparty")
file(MAKE_DIRECTORY "${YAAFC_3RDPARTY_CACHE_DIR}")
file(MAKE_DIRECTORY "${YAAFC_3RDPARTY_OUTPUT_DIR}")

function(yaafc_3rdparty_target_platform OUT_VAR)
    if(APPLE)
        if(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
            set(_P "macos-x86_64")
        else()
            set(_P "macos-arm64")
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

function(yaafc_3rdparty_fetch LIB_NAME)
    set(_LIB_DIR  "${YAAFC_ROOT}/build-tools/3rdparty/${LIB_NAME}")
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

    yaafc_3rdparty_target_platform(_PLATFORM)

    set(_FILENAME "${LIB_NAME}-${_PLATFORM}-${_LIBVER}.tar.gz")
    set(_OUTPUT_DIR "${CMAKE_BINARY_DIR}/3rdparty-cache")
    set(_TARBALL "${_OUTPUT_DIR}/${_FILENAME}")
    set(_DEST "${YAAFC_3RDPARTY_OUTPUT_DIR}/${LIB_NAME}")
    set(_STAMP "${_DEST}/.fetched-${_LIBVER}")

    if(NOT EXISTS "${_STAMP}")
        file(MAKE_DIRECTORY "${_OUTPUT_DIR}")
        if(NOT EXISTS "${_TARBALL}")
            message(STATUS "3rdparty: building ${LIB_NAME} @${_LIBVER} for ${_PLATFORM}")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E env
                    "TARGET_PLATFORM=${_PLATFORM}"
                    "OUTPUT_DIR=${_OUTPUT_DIR}"
                    "CACHE_DIR=${YAAFC_3RDPARTY_CACHE_DIR}"
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
    set(YAAFC_3RDPARTY_${LIB_NAME}_VERSION "${_LIBVER}" CACHE INTERNAL "")
    set(YAAFC_3RDPARTY_${LIB_NAME}_DIR     "${_DEST}"   CACHE INTERNAL "")
endfunction()
