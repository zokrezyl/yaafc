#!/bin/bash
# Builds libyaml (yaml/libyaml) for $TARGET_PLATFORM via its upstream CMake.
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 | macos-arm64 | macos-x86_64
#   OUTPUT_DIR        where the tarball is written
#
# Tarball layout (consumed by build-tools/yaafc/libs/yaml.cmake):
#   lib/libyaml.a
#   include/yaml.h

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"
[ -n "$VERSION" ] || { echo "empty version file" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-/tmp/yaafc-3rdparty-libyaml-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yaafc-3rdparty}"
NCPU="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

LIBYAML_URL="https://github.com/yaml/libyaml/archive/refs/tags/${VERSION}.tar.gz"
LIBYAML_TARBALL="$CACHE_DIR/libyaml-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/libyaml-${VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/libyaml-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

if [ ! -f "$LIBYAML_TARBALL" ]; then
    echo "==> downloading libyaml ${VERSION}"
    curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
        -o "$LIBYAML_TARBALL.part" "$LIBYAML_URL"
    mv "$LIBYAML_TARBALL.part" "$LIBYAML_TARBALL"
fi
if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting -> $SRC_DIR"
    tar -C "$WORK_DIR" -xzf "$LIBYAML_TARBALL"
fi
rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR" "$STAGE"

CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_TESTING=OFF
    -DINSTALL_CMAKE_DIR=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    # libyaml 0.2.5 declares cmake_minimum_required < 3.5; modern CMake
    # refuses without this policy override.
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)

case "$TARGET_PLATFORM" in
linux-x86_64) ;;
linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-linux-gnu-}"
    CMAKE_ARGS+=(
        "-DCMAKE_SYSTEM_NAME=Linux"
        "-DCMAKE_SYSTEM_PROCESSOR=aarch64"
        "-DCMAKE_C_COMPILER=${CROSS_PREFIX}gcc"
    )
    ;;
linux-riscv64)
    : "${CROSS_PREFIX:=riscv64-linux-gnu-}"
    CMAKE_ARGS+=(
        "-DCMAKE_SYSTEM_NAME=Linux"
        "-DCMAKE_SYSTEM_PROCESSOR=riscv64"
        "-DCMAKE_C_COMPILER=${CROSS_PREFIX}gcc"
    )
    ;;
macos-x86_64) CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=x86_64") ;;
macos-arm64)  CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=arm64")  ;;
*) echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac

echo "==> configuring libyaml for $TARGET_PLATFORM"
cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja "${CMAKE_ARGS[@]}"
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

# libyaml installs `libyaml.a` on POSIX and `yaml.lib` on MSVC. We test
# for both because the staging step copies whatever CMake produced.
_LIB=""
for cand in "$STAGE/lib/libyaml.a" "$STAGE/lib/yaml.lib"; do
    if [ -f "$cand" ]; then _LIB="$cand"; break; fi
done
if [ -z "$_LIB" ]; then
    echo "missing libyaml static archive in stage" >&2
    find "$STAGE" >&2
    exit 1
fi
[ -f "$STAGE/include/yaml.h" ] || { echo "missing yaml.h" >&2; exit 1; }

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .
echo "libyaml $VERSION ($TARGET_PLATFORM) ready (lib: $(basename "$_LIB"))"
ls -lh "$TARBALL"
