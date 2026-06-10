#!/bin/bash
# Builds libuv (libuv/libuv) for $TARGET_PLATFORM via its upstream CMake.
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 | macos-arm64 | macos-x86_64
#   OUTPUT_DIR        where the tarball is written
#
# Tarball layout (consumed by build-tools/picomesh/libs/uv.cmake):
#   lib/libuv_a.a
#   include/uv.h, include/uv/...

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"
[ -n "$VERSION" ] || { echo "empty version file" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-/tmp/picomesh-3rdparty-libuv-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/picomesh-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

LIBUV_URL="https://github.com/libuv/libuv/archive/refs/tags/v${VERSION}.tar.gz"
LIBUV_TARBALL="$CACHE_DIR/libuv-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/libuv-${VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/libuv-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

if [ ! -f "$LIBUV_TARBALL" ]; then
    echo "==> downloading libuv ${VERSION}"
    curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
        -o "$LIBUV_TARBALL.part" "$LIBUV_URL"
    mv "$LIBUV_TARBALL.part" "$LIBUV_TARBALL"
fi
if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting -> $SRC_DIR"
    tar -C "$WORK_DIR" -xzf "$LIBUV_TARBALL"
fi
rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE/lib"

CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    -DCMAKE_BUILD_TYPE=Release
    -DLIBUV_BUILD_SHARED=OFF
    -DLIBUV_BUILD_TESTS=OFF
    -DLIBUV_BUILD_BENCH=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

case "$TARGET_PLATFORM" in
linux-x86_64) ;;
linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-linux-gnu-}"
    CMAKE_ARGS+=(
        "-DCMAKE_SYSTEM_NAME=Linux"
        "-DCMAKE_SYSTEM_PROCESSOR=aarch64"
        "-DCMAKE_C_COMPILER=${CROSS_PREFIX}gcc"
        "-DCMAKE_CXX_COMPILER=${CROSS_PREFIX}g++"
    )
    ;;
linux-riscv64)
    : "${CROSS_PREFIX:=riscv64-linux-gnu-}"
    CMAKE_ARGS+=(
        "-DCMAKE_SYSTEM_NAME=Linux"
        "-DCMAKE_SYSTEM_PROCESSOR=riscv64"
        "-DCMAKE_C_COMPILER=${CROSS_PREFIX}gcc"
        "-DCMAKE_CXX_COMPILER=${CROSS_PREFIX}g++"
    )
    ;;
macos-x86_64) CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=x86_64") ;;
macos-arm64)  CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=arm64") ;;
windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell (x64). libuv's
    # upstream CMake supports MSVC; cmake's default Ninja + cl.exe pickup
    # builds it, so no extra args are needed.
    command -v cl >/dev/null 2>&1 || command -v cl.exe >/dev/null 2>&1 || {
        echo "windows-x86_64 requires MSVC cl on PATH (run vcvarsall x64)" >&2; exit 1; }
    ;;
*) echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac

echo "==> configuring libuv for $TARGET_PLATFORM"
cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja "${CMAKE_ARGS[@]}"
echo "==> building (-j${NCPU})"
cmake --build "$BUILD_DIR" -j"$NCPU"
echo "==> installing"
cmake --install "$BUILD_DIR"

for _D in lib lib64; do
    if [ -d "$INSTALL_DIR/$_D" ]; then
        cp -a "$INSTALL_DIR/$_D/." "$STAGE/lib/"
    fi
done
cp -a "$INSTALL_DIR/include" "$STAGE/"

_LIB=""
# POSIX builds produce libuv_a.a / libuv.a; native MSVC produces libuv.lib
# (older configs uv_a.lib). Accept whichever CMake staged.
for cand in "$STAGE/lib/libuv_a.a" "$STAGE/lib/libuv.a" \
            "$STAGE/lib/libuv.lib" "$STAGE/lib/uv_a.lib"; do
    if [ -f "$cand" ]; then _LIB="$cand"; break; fi
done
if [ -z "$_LIB" ]; then
    echo "missing libuv static lib (libuv_a.a / libuv.a / libuv.lib) in stage" >&2
    find "$STAGE" >&2
    exit 1
fi
[ -f "$STAGE/include/uv.h" ] || { echo "missing uv.h" >&2; exit 1; }

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .
echo "libuv $VERSION ($TARGET_PLATFORM) ready (lib: $(basename "$_LIB"))"
ls -lh "$TARBALL"
