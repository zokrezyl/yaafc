#!/bin/bash
# Builds libcurl (curl/curl) for $TARGET_PLATFORM as a STATIC library, linked
# against the sibling OpenSSL recipe for TLS. The build is deliberately minimal:
# HTTP + HTTPS only, OpenSSL TLS backend, no zlib/brotli/zstd compression, no
# HTTP/2 or QUIC, no SSH/LDAP/PSL/IDN. picomesh's only use of libcurl is the
# GitHub OAuth bridge (github_authn) doing plain HTTPS GET/POST — that needs
# nothing more, and a small surface keeps the static lib portable.
#
# OpenSSL is staged the same way libgit2 stages zlib-ng: prefer the published
# prebuilt (`lib-openssl-<ver>`), fall back to building it from the sibling
# recipe on a miss (offline / not yet released). The resulting libcurl.a carries
# UNDEFINED libssl/libcrypto symbols; the consumer resolves them by linking the
# openssl archives at the final link (see build-tools/picomesh/libs/libcurl.cmake).
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 | linux-riscv64 |
#                     macos-arm64 | macos-x86_64
#   OUTPUT_DIR        where the tarball is written
#
# Tarball layout (consumed by build-tools/picomesh/libs/libcurl.cmake):
#   lib/libcurl.a
#   include/curl/...

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"
[ -n "$VERSION" ] || { echo "version file is empty" >&2; exit 1; }

# Upstream tag is `curl-<ver-with-underscores>`; archive top-level dir is
# `curl-curl-<ver-with-underscores>`.
CURL_VER_TAG="$(echo "$VERSION" | tr '.' '_')"
URL="https://github.com/curl/curl/archive/refs/tags/curl-${CURL_VER_TAG}.tar.gz"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/picomesh-3rdparty}"
TARBALL_CACHE="$CACHE_DIR/curl-${VERSION}.tar.gz"
WORK_DIR="${WORK_DIR:-/tmp/picomesh-3rdparty-libcurl-$TARGET_PLATFORM}"
SRC_DIR="$WORK_DIR/curl-curl-${CURL_VER_TAG}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/libcurl-${TARGET_PLATFORM}-${VERSION}.tar.gz"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

# --- stage OpenSSL (TLS backend) -----------------------------------------
# Prefer the published prebuilt for this platform; on a miss fall back to
# building it from the sibling recipe. Both yield lib/lib{ssl,crypto}.a +
# include/openssl/.
OSSL_VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/../openssl/version")"
OSSL_DIR="$WORK_DIR/openssl-${OSSL_VERSION}-${TARGET_PLATFORM}"
OSSL_TAR_NAME="openssl-${TARGET_PLATFORM}-${OSSL_VERSION}.tar.gz"
OSSL_TAR_CACHE="$CACHE_DIR/${OSSL_TAR_NAME}"
URL_BASE="${PICOMESH_3RDPARTY_URL_BASE:-https://github.com/zokrezyl/picomesh/releases/download}"
OSSL_URL="${URL_BASE}/lib-openssl-${OSSL_VERSION}/${OSSL_TAR_NAME}"

# Resolve the staged openssl archives — lib{ssl,crypto}.a on POSIX,
# lib{ssl,crypto}.lib on native MSVC.
ossl_archive() {  # $1 = ssl | crypto
    for _cand in "$OSSL_DIR/lib/lib$1.a" "$OSSL_DIR/lib/lib$1.lib"; do
        [ -f "$_cand" ] && { echo "$_cand"; return 0; }
    done
    return 1
}

if ! ossl_archive ssl >/dev/null; then
    rm -rf "$OSSL_DIR"; mkdir -p "$OSSL_DIR"
    if [ ! -f "$OSSL_TAR_CACHE" ]; then
        echo "==> fetching prebuilt openssl ${OSSL_VERSION} ($TARGET_PLATFORM)"
        if curl -fL --retry 5 --retry-delay 3 --retry-all-errors \
                -o "$OSSL_TAR_CACHE.part" "$OSSL_URL"; then
            mv "$OSSL_TAR_CACHE.part" "$OSSL_TAR_CACHE"
        else
            echo "==> no prebuilt openssl — building it from the sibling recipe"
            rm -f "$OSSL_TAR_CACHE.part"
            OUTPUT_DIR="$(dirname "$OSSL_TAR_CACHE")" \
            TARGET_PLATFORM="$TARGET_PLATFORM" \
            CACHE_DIR="$CACHE_DIR" \
            WORK_DIR="$WORK_DIR/openssl-src" \
                bash "$SCRIPT_DIR/../openssl/_build.sh"
        fi
    fi
    tar -C "$OSSL_DIR" -xzf "$OSSL_TAR_CACHE"
fi
OSSL_SSL_LIB="$(ossl_archive ssl)"       || { echo "openssl staging failed: no libssl"    >&2; exit 1; }
OSSL_CRYPTO_LIB="$(ossl_archive crypto)" || { echo "openssl staging failed: no libcrypto" >&2; exit 1; }
[ -f "$OSSL_DIR/include/openssl/ssl.h" ] || { echo "openssl staging failed: no ssl.h"   >&2; exit 1; }

# --- fetch curl ----------------------------------------------------------
if [ ! -f "$TARBALL_CACHE" ]; then
    echo "==> downloading curl ${VERSION}"
    curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
        -o "$TARBALL_CACHE.part" "$URL"
    mv "$TARBALL_CACHE.part" "$TARBALL_CACHE"
fi
if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting -> $SRC_DIR"
    tar -C "$WORK_DIR" -xzf "$TARBALL_CACHE"
fi
rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"

CMAKE_EXTRA=()
case "$TARGET_PLATFORM" in
linux-x86_64) : ;;
linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-linux-gnu-}"
    CMAKE_EXTRA+=("-DCMAKE_SYSTEM_NAME=Linux" "-DCMAKE_SYSTEM_PROCESSOR=aarch64"
                  "-DCMAKE_C_COMPILER=${CROSS_PREFIX}gcc")
    ;;
linux-riscv64)
    : "${CROSS_PREFIX:=riscv64-linux-gnu-}"
    CMAKE_EXTRA+=("-DCMAKE_SYSTEM_NAME=Linux" "-DCMAKE_SYSTEM_PROCESSOR=riscv64"
                  "-DCMAKE_C_COMPILER=${CROSS_PREFIX}gcc")
    ;;
macos-x86_64) CMAKE_EXTRA+=("-DCMAKE_OSX_ARCHITECTURES=x86_64") ;;
macos-arm64)  CMAKE_EXTRA+=("-DCMAKE_OSX_ARCHITECTURES=arm64")  ;;
windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell (x64). cmake picks
    # up cl.exe via auto-detection. SCHANNEL is forced OFF so curl uses our
    # prebuilt OpenSSL only (one TLS backend across every platform).
    command -v cl >/dev/null 2>&1 || command -v cl.exe >/dev/null 2>&1 || {
        echo "windows-x86_64 requires MSVC cl on PATH (run vcvarsall x64)" >&2; exit 1; }
    CMAKE_EXTRA+=("-DCURL_USE_SCHANNEL=OFF")
    ;;
*) echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac

echo "==> configuring libcurl ${VERSION} (TLS: openssl ${OSSL_VERSION})"
cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_STATIC_LIBS=ON \
    -DBUILD_CURL_EXE=OFF \
    -DBUILD_TESTING=OFF \
    -DBUILD_LIBCURL_DOCS=OFF \
    -DBUILD_MISC_DOCS=OFF \
    -DCURL_USE_OPENSSL=ON \
    -DOPENSSL_ROOT_DIR="$OSSL_DIR" \
    -DOPENSSL_INCLUDE_DIR="$OSSL_DIR/include" \
    -DOPENSSL_SSL_LIBRARY="$OSSL_SSL_LIB" \
    -DOPENSSL_CRYPTO_LIBRARY="$OSSL_CRYPTO_LIB" \
    -DCURL_ZLIB=OFF \
    -DCURL_BROTLI=OFF \
    -DCURL_ZSTD=OFF \
    -DUSE_NGHTTP2=OFF \
    -DUSE_NGTCP2=OFF \
    -DCURL_USE_LIBSSH2=OFF \
    -DCURL_USE_LIBPSL=OFF \
    -DUSE_LIBIDN2=OFF \
    -DCURL_USE_GSSAPI=OFF \
    -DCURL_DISABLE_LDAP=ON \
    -DCURL_DISABLE_LDAPS=ON \
    -DENABLE_UNIX_SOCKETS=OFF \
    -DCMAKE_DISABLE_FIND_PACKAGE_Libidn2=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_LibPSL=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_LibSSH2=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_ZSTD=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_NGHTTP2=ON \
    "${CMAKE_EXTRA[@]}"

echo "==> building (-j${NCPU})"
cmake --build "$BUILD_DIR" -j"$NCPU"

echo "==> installing"
cmake --install "$BUILD_DIR"

mkdir -p "$STAGE/lib"
for _D in lib lib64; do
    if [ -d "$INSTALL_DIR/$_D" ]; then
        cp -a "$INSTALL_DIR/$_D/." "$STAGE/lib/"
    fi
done
cp -a "$INSTALL_DIR/include" "$STAGE/"

# POSIX builds produce libcurl.a; native MSVC produces libcurl.lib.
[ -f "$STAGE/lib/libcurl.a" ] || [ -f "$STAGE/lib/libcurl.lib" ] || {
    echo "missing libcurl.a / libcurl.lib in stage" >&2; find "$STAGE" -maxdepth 3 >&2; exit 1; }
[ -f "$STAGE/include/curl/curl.h" ]    || { echo "missing include/curl/curl.h" >&2; exit 1; }

# The consumer wraps the archive as an IMPORTED target; CMake/pkgconfig modules
# and any stray shared libs are unused.
rm -rf "$STAGE/lib/cmake" "$STAGE/lib/pkgconfig"
find "$STAGE/lib" -maxdepth 1 -name 'libcurl.so*' -delete 2>/dev/null || true

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .
echo "libcurl $VERSION + openssl ${OSSL_VERSION} ($TARGET_PLATFORM) ready"
ls -lh "$TARBALL"
