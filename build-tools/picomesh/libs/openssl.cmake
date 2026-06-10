# OpenSSL — static TLS backend for libcurl. Built from source (no-shared) by
# build-tools/3rdparty/openssl/_build.sh and shipped as lib/lib{ssl,crypto}.a +
# include/openssl/. Prebuilt tarball is fetched (download-or-build) by
# 3rdparty-fetch.cmake. Exposed as a single INTERFACE target `openssl` that
# pulls in libssl (which depends on libcrypto) plus the OS deps OpenSSL needs at
# the final link (dl + pthread on Linux).

include_guard(GLOBAL)
include(${PICOMESH_ROOT}/build-tools/picomesh/3rdparty-fetch.cmake)

if(TARGET openssl)
    return()
endif()

picomesh_3rdparty_fetch(openssl _OSSL_DIR)

set(_OSSL_SSL "${_OSSL_DIR}/lib/libssl.a")
set(_OSSL_CRYPTO "${_OSSL_DIR}/lib/libcrypto.a")
foreach(_lib "${_OSSL_SSL}" "${_OSSL_CRYPTO}")
    if(NOT EXISTS "${_lib}")
        message(FATAL_ERROR "openssl: ${_lib} missing")
    endif()
endforeach()
if(NOT EXISTS "${_OSSL_DIR}/include/openssl/ssl.h")
    message(FATAL_ERROR "openssl: include/openssl/ssl.h missing")
endif()

find_package(Threads REQUIRED)

add_library(openssl-crypto STATIC IMPORTED GLOBAL)
set_target_properties(openssl-crypto PROPERTIES
    IMPORTED_LOCATION             "${_OSSL_CRYPTO}"
    INTERFACE_INCLUDE_DIRECTORIES "${_OSSL_DIR}/include"
)
# libcrypto's system deps: POSIX uses pthread + dl; native Windows pulls the
# Win32 socket/crypto/registry libs (ws2_32 for BIO sockets, crypt32 + bcrypt
# for the OS RNG/cert store, advapi32/user32 for registry + UI entropy).
if(WIN32)
    target_link_libraries(openssl-crypto INTERFACE ws2_32 crypt32 bcrypt advapi32 user32)
else()
    target_link_libraries(openssl-crypto INTERFACE Threads::Threads ${CMAKE_DL_LIBS})
endif()

add_library(openssl-ssl STATIC IMPORTED GLOBAL)
set_target_properties(openssl-ssl PROPERTIES
    IMPORTED_LOCATION             "${_OSSL_SSL}"
    INTERFACE_INCLUDE_DIRECTORIES "${_OSSL_DIR}/include"
)
# libssl.a references libcrypto.a — crypto must follow ssl on the link line,
# which an INTERFACE dependency guarantees.
target_link_libraries(openssl-ssl INTERFACE openssl-crypto)

# Single umbrella target consumers link.
add_library(openssl INTERFACE IMPORTED GLOBAL)
set_target_properties(openssl PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_OSSL_DIR}/include"
)
target_link_libraries(openssl INTERFACE openssl-ssl)

message(STATUS "openssl: prebuilt v${PICOMESH_3RDPARTY_openssl_VERSION} (${_OSSL_SSL})")
