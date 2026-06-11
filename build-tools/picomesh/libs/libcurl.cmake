# libcurl — HTTPS client (used only by the github_authn OAuth bridge). Built
# minimal (HTTP/HTTPS only, OpenSSL TLS, no compression/HTTP2/QUIC/SSH) by
# build-tools/3rdparty/libcurl/_build.sh and shipped as lib/libcurl.a +
# include/curl/. Prebuilt tarball is fetched (download-or-build) by
# 3rdparty-fetch.cmake. libcurl.a carries undefined libssl/libcrypto symbols;
# the openssl IMPORTED lib resolves them at the final link (it follows libcurl
# on the link line as an INTERFACE dependency).

include_guard(GLOBAL)
include(${PICOMESH_ROOT}/build-tools/picomesh/3rdparty-fetch.cmake)

if(TARGET libcurl)
    return()
endif()

picomesh_3rdparty_fetch(libcurl _CURL_DIR)

set(_CURL_LIB "${_CURL_DIR}/lib/libcurl.a")
if(NOT EXISTS "${_CURL_LIB}")
    message(FATAL_ERROR "libcurl: ${_CURL_LIB} missing")
endif()
if(NOT EXISTS "${_CURL_DIR}/include/curl/curl.h")
    message(FATAL_ERROR "libcurl: include/curl/curl.h missing")
endif()

include(${PICOMESH_ROOT}/build-tools/picomesh/libs/openssl.cmake)
find_package(Threads REQUIRED)

add_library(libcurl STATIC IMPORTED GLOBAL)
set_target_properties(libcurl PROPERTIES
    IMPORTED_LOCATION             "${_CURL_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_CURL_DIR}/include"
    # A statically-linked libcurl must see CURL_STATICLIB so its headers don't
    # decorate the API with dllimport.
    INTERFACE_COMPILE_DEFINITIONS "CURL_STATICLIB"
)
# Beyond openssl: POSIX needs pthread + dl; native Windows needs the Win32
# socket + crypto libs curl calls directly (ws2_32 sockets, wldap32 even with
# LDAP disabled the import is referenced, crypt32/bcrypt for the cert path).
if(WIN32)
    target_link_libraries(libcurl INTERFACE openssl ws2_32 crypt32 bcrypt advapi32 normaliz)
else()
    target_link_libraries(libcurl INTERFACE openssl Threads::Threads ${CMAKE_DL_LIBS})
    if(APPLE)
        # Curl_macos_init reads the system proxy config via SystemConfiguration
        # (which pulls CoreFoundation: CFRelease, SCDynamicStoreCopyProxies).
        target_link_libraries(libcurl INTERFACE
            "-framework CoreFoundation" "-framework SystemConfiguration")
    endif()
endif()

message(STATUS "libcurl: prebuilt v${PICOMESH_3RDPARTY_libcurl_VERSION} (${_CURL_LIB})")
