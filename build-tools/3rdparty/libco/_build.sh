#!/bin/bash
# Builds libco (higan-emu/libco) for $TARGET_PLATFORM.
#
# Single .c file; libco selects the stack-switch backend by #ifdef
# (amd64/aarch64/arm/x86/ppc → tiny inline asm; otherwise ucontext).
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 | macos-arm64 | ...
#   OUTPUT_DIR        where the tarball is written
#
# Reads the upstream pin from sibling `version` file. libco has no
# tagged releases — pin via commit SHA.
#
# Tarball layout (consumed by build-tools/yaafc/libs/co.cmake):
#   lib/libco.a
#   include/libco.h

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"
[ -n "$VERSION" ] || { echo "empty version file" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-/tmp/yaafc-3rdparty-libco-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yaafc-3rdparty}"

LIBCO_URL="https://github.com/higan-emu/libco/archive/${VERSION}.tar.gz"
LIBCO_TARBALL="$CACHE_DIR/libco-${VERSION}.tar.gz"
SRC_DIR="$WORK_DIR/libco-${VERSION}"
INSTALL_DIR="$WORK_DIR/install-${TARGET_PLATFORM}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/libco-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

if [ ! -f "$LIBCO_TARBALL" ]; then
    echo "==> downloading libco @${VERSION:0:8}"
    curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
        -o "$LIBCO_TARBALL.part" "$LIBCO_URL"
    mv "$LIBCO_TARBALL.part" "$LIBCO_TARBALL"
fi

if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting -> $SRC_DIR"
    rm -rf "$WORK_DIR/.extract-$$"
    mkdir -p "$WORK_DIR/.extract-$$"
    tar -C "$WORK_DIR/.extract-$$" -xzf "$LIBCO_TARBALL"
    mv "$WORK_DIR/.extract-$$/libco-${VERSION}" "$SRC_DIR"
    rmdir "$WORK_DIR/.extract-$$"
fi
rm -rf "$INSTALL_DIR" "$STAGE"
mkdir -p "$INSTALL_DIR/lib" "$INSTALL_DIR/include" "$STAGE"

CFLAGS_BASE="-O2 -fPIC -std=c99"
CC=cc
AR=ar
CFLAGS_EXTRA=""

case "$TARGET_PLATFORM" in
linux-x86_64)
    CC=gcc
    ;;
linux-aarch64)
    : "${CROSS_PREFIX:=aarch64-linux-gnu-}"
    CC="${CROSS_PREFIX}gcc"
    AR="${CROSS_PREFIX}ar"
    ;;
linux-riscv64)
    : "${CROSS_PREFIX:=riscv64-linux-gnu-}"
    CC="${CROSS_PREFIX}gcc"
    AR="${CROSS_PREFIX}ar"
    ;;
macos-x86_64)
    CC=clang
    CFLAGS_EXTRA="-arch x86_64"
    ;;
macos-arm64)
    CC=clang
    CFLAGS_EXTRA="-arch arm64"
    ;;
*)
    echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2
    exit 1
    ;;
esac

CFLAGS="$CFLAGS_BASE $CFLAGS_EXTRA"

echo "==> compiling libco for $TARGET_PLATFORM"
$CC $CFLAGS -I"$SRC_DIR" -c "$SRC_DIR/libco.c" -o "$WORK_DIR/libco.o"
$AR rcs "$INSTALL_DIR/lib/libco.a" "$WORK_DIR/libco.o"
cp "$SRC_DIR/libco.h" "$INSTALL_DIR/include/"

cp -a "$INSTALL_DIR/lib"     "$STAGE/"
cp -a "$INSTALL_DIR/include" "$STAGE/"
[ -f "$STAGE/lib/libco.a"     ] || { echo "missing libco.a in stage" >&2; exit 1; }
[ -f "$STAGE/include/libco.h" ] || { echo "missing libco.h in stage" >&2; exit 1; }

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "libco @${VERSION:0:8} ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
