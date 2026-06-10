#!/bin/bash
# Builds libgit2 for $TARGET_PLATFORM as a static library. Network
# transports (https, ssh, ntlm) and the external http-parser/regex are
# compiled out; zlib is provided by zlib-ng (USE_BUNDLED_ZLIB=OFF) for its
# SIMD deflate/inflate, staged from the sibling zlib-ng recipe below. The
# resulting libgit2.a carries UNDEFINED zlib symbols (deflate/inflate); the
# consumer resolves them by linking zlib-ng's libz.a at the final link (see
# build-tools/picomesh/libs/libgit2.cmake). At runtime it then needs only
# libc + pthread + zlib-ng, which keeps the BSL-license combined work clean.
#
# License posture: libgit2 is GPLv2 with a linking exception that
# explicitly permits combining with code under any other license,
# including BSL — see libgit2's COPYING. We do NOT modify libgit2
# sources themselves; if a future patch becomes necessary it would
# have to be made available under GPLv2 (covered by the exception
# the same way the unmodified library is covered).
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 | macos-arm64 | macos-x86_64
#   OUTPUT_DIR        where the tarball is written
#
# Tarball layout (consumed by build-tools/picomesh/libs/libgit2.cmake):
#   lib/libgit2.a
#   include/git2.h
#   include/git2/...

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"

URL="https://github.com/libgit2/libgit2/archive/refs/tags/v${VERSION}.tar.gz"
TARBALL_CACHE="$HOME/.cache/picomesh-3rdparty/libgit2-${VERSION}.tar.gz"
WORK_DIR="${WORK_DIR:-/tmp/picomesh-3rdparty-libgit2-$TARGET_PLATFORM}"
SRC_DIR="$WORK_DIR/libgit2-${VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/libgit2-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$(dirname "$TARBALL_CACHE")"

if [ ! -f "$TARBALL_CACHE" ]; then
    echo "==> downloading libgit2 ${VERSION}"
    curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
        -o "$TARBALL_CACHE.part" "$URL"
    mv "$TARBALL_CACHE.part" "$TARBALL_CACHE"
fi
if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting"
    tar -C "$WORK_DIR" -xzf "$TARBALL_CACHE"
fi
rm -rf "$STAGE" "$BUILD_DIR"
mkdir -p "$STAGE" "$BUILD_DIR"

CC=cc
CXX=c++
CMAKE_GEN="Unix Makefiles"
CMAKE_EXTRA=()
case "$TARGET_PLATFORM" in
linux-x86_64) CC=gcc; CXX=g++ ;;
linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-linux-gnu-}"
    CC="${CROSS_PREFIX}gcc"
    CXX="${CROSS_PREFIX}g++"
    CMAKE_EXTRA+=("-DCMAKE_SYSTEM_NAME=Linux"
                  "-DCMAKE_SYSTEM_PROCESSOR=aarch64")
    ;;
linux-riscv64)
    : "${CROSS_PREFIX:=riscv64-linux-gnu-}"
    CC="${CROSS_PREFIX}gcc"
    CXX="${CROSS_PREFIX}g++"
    CMAKE_EXTRA+=("-DCMAKE_SYSTEM_NAME=Linux"
                  "-DCMAKE_SYSTEM_PROCESSOR=riscv64")
    ;;
macos-x86_64) CC=clang; CXX=clang++; CMAKE_EXTRA+=("-DCMAKE_OSX_ARCHITECTURES=x86_64") ;;
macos-arm64)  CC=clang; CXX=clang++; CMAKE_EXTRA+=("-DCMAKE_OSX_ARCHITECTURES=arm64")  ;;
windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell (x64). Use Ninja +
    # cl.exe (no `make` for the Unix Makefiles generator on Windows).
    command -v cl >/dev/null 2>&1 || command -v cl.exe >/dev/null 2>&1 || {
        echo "windows-x86_64 requires MSVC cl on PATH (run vcvarsall x64)" >&2; exit 1; }
    CC=cl; CXX=cl
    CMAKE_GEN="Ninja"
    ;;
*) echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac

# --- stage zlib-ng so libgit2 links the SIMD zlib, not the bundled one ----
# Prefer the published prebuilt for this platform; on a miss (offline, not
# yet released) fall back to building it from the sibling recipe. Both yield
# the lib/libz.a + include/zlib.h layout FindZLIB needs.
ZLIBNG_VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/../zlib-ng/version")"
ZLIBNG_DIR="$WORK_DIR/zlib-ng-${ZLIBNG_VERSION}-${TARGET_PLATFORM}"
ZLIBNG_TAR_NAME="zlib-ng-${TARGET_PLATFORM}-${ZLIBNG_VERSION}.tar.gz"
ZLIBNG_TAR_CACHE="$HOME/.cache/picomesh-3rdparty/${ZLIBNG_TAR_NAME}"
URL_BASE="${PICOMESH_3RDPARTY_URL_BASE:-https://github.com/zokrezyl/picomesh/releases/download}"
ZLIBNG_URL="${URL_BASE}/lib-zlib-ng-${ZLIBNG_VERSION}/${ZLIBNG_TAR_NAME}"

# Resolve the staged zlib-ng static archive — libz.a on POSIX, zlibstatic.lib
# (or zlib.lib) on native MSVC.
zlibng_archive() {
    for _cand in "$ZLIBNG_DIR/lib/libz.a" \
                 "$ZLIBNG_DIR/lib/zlibstatic.lib" \
                 "$ZLIBNG_DIR/lib/zlib.lib"; do
        [ -f "$_cand" ] && { echo "$_cand"; return 0; }
    done
    return 1
}

if ! zlibng_archive >/dev/null; then
    rm -rf "$ZLIBNG_DIR"; mkdir -p "$ZLIBNG_DIR"
    if [ ! -f "$ZLIBNG_TAR_CACHE" ]; then
        echo "==> fetching prebuilt zlib-ng ${ZLIBNG_VERSION} ($TARGET_PLATFORM)"
        if curl -fL --retry 5 --retry-delay 3 --retry-all-errors \
                -o "$ZLIBNG_TAR_CACHE.part" "$ZLIBNG_URL"; then
            mv "$ZLIBNG_TAR_CACHE.part" "$ZLIBNG_TAR_CACHE"
        else
            echo "==> no prebuilt zlib-ng — building it from the sibling recipe"
            rm -f "$ZLIBNG_TAR_CACHE.part"
            OUTPUT_DIR="$(dirname "$ZLIBNG_TAR_CACHE")" \
            TARGET_PLATFORM="$TARGET_PLATFORM" \
            WORK_DIR="$WORK_DIR/zlib-ng-src" \
                bash "$SCRIPT_DIR/../zlib-ng/_build.sh"
        fi
    fi
    tar -C "$ZLIBNG_DIR" -xzf "$ZLIBNG_TAR_CACHE"
fi
ZLIBNG_LIB="$(zlibng_archive)" || { echo "zlib-ng staging failed: no static archive" >&2; exit 1; }
[ -f "$ZLIBNG_DIR/include/zlib.h" ] || { echo "zlib-ng staging failed: no zlib.h"   >&2; exit 1; }

echo "==> configuring libgit2 (zlib-ng @ $ZLIBNG_DIR)"
# Minimal build: no network transports (HTTPS, SSH, NTLM), builtin regex,
# builtin http-parser. zlib comes from zlib-ng (USE_BUNDLED_ZLIB=OFF) for SIMD
# deflate/inflate. We only need libgit2 for local-filesystem repo work (init,
# tree, blob, commit walks).
cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G "$CMAKE_GEN" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_INSTALL_PREFIX="$STAGE" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_TESTS=OFF \
    -DBUILD_CLI=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DUSE_HTTPS=OFF \
    -DUSE_SSH=OFF \
    -DUSE_NTLMCLIENT=OFF \
    -DUSE_BUNDLED_ZLIB=OFF \
    -DZLIB_ROOT="$ZLIBNG_DIR" \
    -DZLIB_LIBRARY="$ZLIBNG_LIB" \
    -DZLIB_INCLUDE_DIR="$ZLIBNG_DIR/include" \
    -DREGEX_BACKEND=builtin \
    -DUSE_HTTP_PARSER=builtin \
    "${CMAKE_EXTRA[@]}"

echo "==> building libgit2"
cmake --build "$BUILD_DIR" --parallel "$(nproc 2>/dev/null || echo 4)"

echo "==> installing libgit2 into stage"
cmake --install "$BUILD_DIR"

# libgit2's CMake install lays out lib/libgit2.a + include/git2.h +
# include/git2/. Confirm we got what the consumer expects.
# POSIX builds produce libgit2.a; native MSVC produces git2.lib.
[ -f "$STAGE/lib/libgit2.a" ] || [ -f "$STAGE/lib/git2.lib" ] || {
    echo "missing libgit2.a / git2.lib in $STAGE/lib" >&2; ls -la "$STAGE/lib" || true; exit 1; }
[ -f "$STAGE/include/git2.h" ]   || { echo "missing git2.h in $STAGE/include" >&2; exit 1; }

# Trim non-essential install artefacts so the tarball stays slim. The
# CMake / pkgconfig modules aren't read by the consumer (libgit2.cmake
# under build-tools/picomesh/libs/ wraps it as an IMPORTED target).
rm -rf "$STAGE/lib/pkgconfig" "$STAGE/lib/cmake" "$STAGE/share"

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .
echo "libgit2 $VERSION ($TARGET_PLATFORM) ready"
ls -lh "$TARBALL"
