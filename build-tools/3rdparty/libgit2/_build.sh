#!/bin/bash
# Builds libgit2 for $TARGET_PLATFORM as a single static library, with
# every transitive dependency either bundled (zlib) or compiled out
# (https, ssh, ntlm, external http-parser, external regex). The
# resulting libgit2.a is self-contained: it only links against libc +
# pthread at runtime, which makes the BSL-license combined work clean.
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
# Tarball layout (consumed by build-tools/yaafc/libs/libgit2.cmake):
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
TARBALL_CACHE="$HOME/.cache/yaafc-3rdparty/libgit2-${VERSION}.tar.gz"
WORK_DIR="${WORK_DIR:-/tmp/yaafc-3rdparty-libgit2-$TARGET_PLATFORM}"
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
*) echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac

echo "==> configuring libgit2"
# Minimal build: no network transports (HTTPS, SSH, NTLM), bundled
# zlib, builtin regex, builtin http-parser. We only need libgit2 for
# local-filesystem repo work (init, tree, blob, commit walks).
cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G "Unix Makefiles" \
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
    -DUSE_BUNDLED_ZLIB=ON \
    -DREGEX_BACKEND=builtin \
    -DUSE_HTTP_PARSER=builtin \
    "${CMAKE_EXTRA[@]}"

echo "==> building libgit2"
cmake --build "$BUILD_DIR" --parallel "$(nproc 2>/dev/null || echo 4)"

echo "==> installing libgit2 into stage"
cmake --install "$BUILD_DIR"

# libgit2's CMake install lays out lib/libgit2.a + include/git2.h +
# include/git2/. Confirm we got what the consumer expects.
[ -f "$STAGE/lib/libgit2.a" ]    || { echo "missing libgit2.a in $STAGE/lib" >&2; ls -la "$STAGE/lib" || true; exit 1; }
[ -f "$STAGE/include/git2.h" ]   || { echo "missing git2.h in $STAGE/include" >&2; exit 1; }

# Trim non-essential install artefacts so the tarball stays slim. The
# CMake / pkgconfig modules aren't read by the consumer (libgit2.cmake
# under build-tools/yaafc/libs/ wraps it as an IMPORTED target).
rm -rf "$STAGE/lib/pkgconfig" "$STAGE/lib/cmake" "$STAGE/share"

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .
echo "libgit2 $VERSION ($TARGET_PLATFORM) ready"
ls -lh "$TARBALL"
