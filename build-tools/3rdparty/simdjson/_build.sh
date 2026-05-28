#!/bin/bash
# Builds simdjson (simdjson/simdjson) for $TARGET_PLATFORM via its
# upstream CMake project. Static library only.
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 | macos-arm64 | macos-x86_64
#   OUTPUT_DIR        where the tarball is written
#
# Tarball layout (consumed by build-tools/yaafc/libs/simdjson.cmake):
#   lib/libsimdjson.a
#   include/simdjson.h, simdjson/...

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"
[ -n "$VERSION" ] || { echo "empty version file" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-/tmp/yaafc-3rdparty-simdjson-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yaafc-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

SJ_URL="https://github.com/simdjson/simdjson/archive/refs/tags/v${VERSION}.tar.gz"
SJ_TARBALL="$CACHE_DIR/simdjson-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/simdjson-${VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/simdjson-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

if [ ! -f "$SJ_TARBALL" ]; then
    echo "==> downloading simdjson ${VERSION}"
    curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
        -o "$SJ_TARBALL.part" "$SJ_URL"
    mv "$SJ_TARBALL.part" "$SJ_TARBALL"
fi
if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting -> $SRC_DIR"
    tar -C "$WORK_DIR" -xzf "$SJ_TARBALL"
fi
rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE/lib"

CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    -DCMAKE_BUILD_TYPE=Release
    -DSIMDJSON_BUILD_STATIC_LIB=ON
    -DBUILD_SHARED_LIBS=OFF
    -DSIMDJSON_DEVELOPER_MODE=OFF
    -DSIMDJSON_JUST_LIBRARY=ON
    -DSIMDJSON_ENABLE_THREADS=OFF
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
*) echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac

echo "==> configuring simdjson for $TARGET_PLATFORM"
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

[ -f "$STAGE/lib/libsimdjson.a" ] || { echo "missing libsimdjson.a" >&2; find "$STAGE" >&2; exit 1; }
[ -f "$STAGE/include/simdjson.h" ] || { echo "missing simdjson.h" >&2; exit 1; }

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .
echo "simdjson $VERSION ($TARGET_PLATFORM) ready"
ls -lh "$TARBALL"
