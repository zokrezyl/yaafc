# libgit2 — local-filesystem git library. GPLv2 with a linking
# exception that explicitly permits combining with code under any
# other license (BSL, proprietary, MIT, …). See the libgit2 source
# tarball's COPYING file or _build.sh comments for the exact wording.

include_guard(GLOBAL)
include(${PICOMESH_ROOT}/build-tools/picomesh/3rdparty-fetch.cmake)

if(TARGET libgit2)
    return()
endif()

picomesh_3rdparty_fetch(libgit2 _LG_DIR)

set(_LG_LIB "${_LG_DIR}/lib/libgit2.a")
if(NOT EXISTS "${_LG_LIB}")
    message(FATAL_ERROR "libgit2: ${_LG_LIB} missing")
endif()
if(NOT EXISTS "${_LG_DIR}/include/git2.h")
    message(FATAL_ERROR "libgit2: include/git2.h missing")
endif()

add_library(libgit2 STATIC IMPORTED GLOBAL)
set_target_properties(libgit2 PROPERTIES
    IMPORTED_LOCATION             "${_LG_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_LG_DIR}/include"
)
# Minimal libgit2 (no HTTPS, no SSH) built against zlib-ng instead of the
# bundled zlib (USE_BUNDLED_ZLIB=OFF — see the _build.sh). libgit2.a therefore
# carries UNDEFINED deflate/inflate symbols; the zlib-ng IMPORTED lib resolves
# them at the final link (it must follow libgit2 on the link line, which it does
# as a libgit2 INTERFACE dependency). Beyond zlib-ng it only needs libc +
# pthread — plus librt on glibc for clock_gettime.
include(${PICOMESH_ROOT}/build-tools/picomesh/libs/zlib-ng.cmake)
find_package(Threads REQUIRED)
# Beyond zlib-ng: POSIX needs librt (clock_gettime on glibc); native Windows
# needs the Win32 system libs libgit2 calls even in the no-HTTPS/no-SSH config
# (UUID generation, crypto for object hashing, sockets, registry/SID lookups).
if(WIN32)
    target_link_libraries(libgit2 INTERFACE zlib-ng
        ws2_32 advapi32 rpcrt4 crypt32 ole32 secur32 winhttp)
else()
    target_link_libraries(libgit2 INTERFACE zlib-ng Threads::Threads)
    if(APPLE)
        # libgit2 normalises HFS+ paths via iconv, a separate lib on macOS.
        target_link_libraries(libgit2 INTERFACE iconv)
    else()
        target_link_libraries(libgit2 INTERFACE rt)  # glibc clock_gettime; not on macOS
    endif()
endif()

message(STATUS "libgit2: prebuilt v${PICOMESH_3RDPARTY_libgit2_VERSION} (${_LG_LIB})")
