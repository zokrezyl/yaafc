# libmdbx — embedded copy-on-write KV engine. Local build via the
# 3rdparty pattern, mirroring sqlite3.cmake.

include_guard(GLOBAL)
include(${PICOMESH_ROOT}/build-tools/picomesh/3rdparty-fetch.cmake)

if(TARGET libmdbx)
    return()
endif()

picomesh_3rdparty_fetch(libmdbx _LM_DIR)

set(_LM_LIB "${_LM_DIR}/lib/libmdbx.a")
if(NOT EXISTS "${_LM_LIB}")
    message(FATAL_ERROR "libmdbx: ${_LM_LIB} missing")
endif()
if(NOT EXISTS "${_LM_DIR}/include/mdbx.h")
    message(FATAL_ERROR "libmdbx: include/mdbx.h missing")
endif()

add_library(libmdbx STATIC IMPORTED GLOBAL)
set_target_properties(libmdbx PROPERTIES
    IMPORTED_LOCATION             "${_LM_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_LM_DIR}/include"
)
# libmdbx uses pthread primitives and clock_gettime for its lock table
# and timestamps. -lrt is harmless on systems where clock_gettime is in
# libc.
find_package(Threads REQUIRED)
# POSIX: pthread + librt (clock_gettime). Native Windows: mdbx.c uses the NT
# native + Win32 APIs (Rtl*, advapi32 crypto for randomness, user/kernel).
if(WIN32)
    target_link_libraries(libmdbx INTERFACE ntdll advapi32 user32 kernel32)
else()
    target_link_libraries(libmdbx INTERFACE Threads::Threads)
    if(NOT APPLE)
        target_link_libraries(libmdbx INTERFACE rt)  # glibc clock_gettime; not on macOS
    endif()
endif()

message(STATUS "libmdbx: prebuilt v${PICOMESH_3RDPARTY_libmdbx_VERSION} (${_LM_LIB})")
