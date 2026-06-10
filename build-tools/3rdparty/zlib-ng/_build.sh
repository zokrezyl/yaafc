#!/bin/bash
# Builds zlib-ng for $TARGET_PLATFORM as a single static library, in
# zlib-compat mode so it is a DROP-IN replacement for stock zlib: it
# installs lib/libz.a + include/zlib.h + include/zconf.h, exactly the
# layout a zlib consumer (e.g. libgit2 built with -DUSE_BUNDLED_ZLIB=OFF
# -DZLIB_ROOT=<this stage>) expects. zlib-ng is the SIMD-accelerated
# deflate/inflate fork; it does runtime CPU-feature dispatch (WITH_OPTIM,
# default ON) so ONE published binary stays portable AND fast across CPUs
# — which is why WITH_NATIVE_INSTRUCTIONS stays OFF (baking -march=native
# into a release artefact would crash on older hosts).
#
# License posture: zlib-ng is under the zlib license (permissive, no
# copyleft), so the combined work has no extra obligations.
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 | linux-riscv64 | macos-arm64 | macos-x86_64
#   OUTPUT_DIR        where the tarball is written
#
# Tarball layout (consumed by build-tools/picomesh/libs/zlib-ng.cmake):
#   lib/libz.a
#   include/zlib.h
#   include/zconf.h

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"

# zlib-ng release tags carry no leading 'v' (e.g. 2.3.3).
URL="https://github.com/zlib-ng/zlib-ng/archive/refs/tags/${VERSION}.tar.gz"
TARBALL_CACHE="$HOME/.cache/picomesh-3rdparty/zlib-ng-${VERSION}.tar.gz"
WORK_DIR="${WORK_DIR:-/tmp/picomesh-3rdparty-zlib-ng-$TARGET_PLATFORM}"
SRC_DIR="$WORK_DIR/zlib-ng-${VERSION}"
BUILD_DIR="$WORK_DIR/build-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/zlib-ng-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$(dirname "$TARBALL_CACHE")"

if [ ! -f "$TARBALL_CACHE" ]; then
    echo "==> downloading zlib-ng ${VERSION}"
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
CMAKE_GEN="Unix Makefiles"
CMAKE_EXTRA=()
case "$TARGET_PLATFORM" in
linux-x86_64) CC=gcc ;;
linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-linux-gnu-}"
    CC="${CROSS_PREFIX}gcc"
    CMAKE_EXTRA+=("-DCMAKE_SYSTEM_NAME=Linux"
                  "-DCMAKE_SYSTEM_PROCESSOR=aarch64")
    ;;
linux-riscv64)
    : "${CROSS_PREFIX:=riscv64-linux-gnu-}"
    CC="${CROSS_PREFIX}gcc"
    CMAKE_EXTRA+=("-DCMAKE_SYSTEM_NAME=Linux"
                  "-DCMAKE_SYSTEM_PROCESSOR=riscv64")
    ;;
macos-x86_64) CC=clang; CMAKE_EXTRA+=("-DCMAKE_OSX_ARCHITECTURES=x86_64") ;;
macos-arm64)  CC=clang; CMAKE_EXTRA+=("-DCMAKE_OSX_ARCHITECTURES=arm64")  ;;
windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell (x64). Use Ninja +
    # cl.exe (there is no `make` for the Unix Makefiles generator on Windows).
    command -v cl >/dev/null 2>&1 || command -v cl.exe >/dev/null 2>&1 || {
        echo "windows-x86_64 requires MSVC cl on PATH (run vcvarsall x64)" >&2; exit 1; }
    CC=cl
    CMAKE_GEN="Ninja"
    ;;
*) echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac

echo "==> configuring zlib-ng"
# ZLIB_COMPAT=ON  → libz.a + zlib.h/zconf.h, a drop-in for stock zlib.
# WITH_OPTIM (default ON) builds the SIMD deflate/inflate paths and selects
# them at runtime; WITH_NATIVE_INSTRUCTIONS stays OFF so the artefact runs
# on any CPU of the target arch (no baked-in -march=native).
cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G "$CMAKE_GEN" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_INSTALL_PREFIX="$STAGE" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_SHARED_LIBS=OFF \
    -DZLIB_COMPAT=ON \
    -DZLIB_ENABLE_TESTS=OFF \
    -DZLIBNG_ENABLE_TESTS=OFF \
    -DWITH_GTEST=OFF \
    -DWITH_NATIVE_INSTRUCTIONS=OFF \
    "${CMAKE_EXTRA[@]}"

echo "==> building zlib-ng"
cmake --build "$BUILD_DIR" --parallel "$(nproc 2>/dev/null || echo 4)"

echo "==> installing zlib-ng into stage"
cmake --install "$BUILD_DIR"

# In ZLIB_COMPAT mode the install lays out lib/libz.a + include/zlib.h +
# include/zconf.h on POSIX. Native MSVC names the static archive
# zlibstatic.lib (some configs zlib.lib) — accept either.
if [ ! -f "$STAGE/lib/libz.a" ] \
    && [ ! -f "$STAGE/lib/zlibstatic.lib" ] \
    && [ ! -f "$STAGE/lib/zlib.lib" ]; then
    echo "missing zlib static lib (libz.a / zlibstatic.lib / zlib.lib) in $STAGE/lib" >&2
    ls -la "$STAGE/lib" || true
    exit 1
fi
[ -f "$STAGE/include/zlib.h" ] || { echo "missing zlib.h in $STAGE/include" >&2; exit 1; }

# Trim non-essential install artefacts so the tarball stays slim. The
# CMake / pkgconfig modules aren't read by the consumer (zlib-ng.cmake
# under build-tools/picomesh/libs/ wraps it as an IMPORTED target).
rm -rf "$STAGE/lib/pkgconfig" "$STAGE/lib/cmake" "$STAGE/share"

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .
echo "zlib-ng $VERSION ($TARGET_PLATFORM) ready"
ls -lh "$TARBALL"
