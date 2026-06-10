#!/bin/bash
# Builds the libmdbx amalgamation for $TARGET_PLATFORM as a single static
# library.
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 | macos-arm64 | macos-x86_64
#   OUTPUT_DIR        where the tarball is written
#
# Tarball layout (consumed by build-tools/picomesh/libs/libmdbx.cmake):
#   lib/libmdbx.a
#   include/mdbx.h
#
# Upstream publishes a single-source "amalgamated" release alongside the
# repository build — same shape as sqlite's amalgamation. We use that
# here to avoid pulling in libmdbx's full CMake build (which needs a
# live git checkout for its version-stamp baking).

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"

# Amalgamation tarball layout from libmdbx.dqdkfa.ru/release/ — note the
# archive is FLAT (files sit at the top level, no enclosing directory):
#   libmdbx-amalgamated-<VERSION>.tar.xz
#     mdbx.c
#     mdbx.h
#     mdbx.h++
#     ...
URL="https://libmdbx.dqdkfa.ru/release/libmdbx-amalgamated-${VERSION}.tar.xz"
TARBALL_CACHE="$HOME/.cache/picomesh-3rdparty/libmdbx-amalgamated-${VERSION}.tar.xz"
WORK_DIR="${WORK_DIR:-/tmp/picomesh-3rdparty-libmdbx-$TARGET_PLATFORM}"
SRC_DIR="$WORK_DIR/src-${VERSION}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/libmdbx-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$(dirname "$TARBALL_CACHE")"

if [ ! -f "$TARBALL_CACHE" ]; then
    echo "==> downloading libmdbx ${VERSION}"
    curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
        -o "$TARBALL_CACHE.part" "$URL"
    mv "$TARBALL_CACHE.part" "$TARBALL_CACHE"
fi
if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting"
    rm -rf "$SRC_DIR"
    mkdir -p "$SRC_DIR"
    tar -C "$SRC_DIR" -xJf "$TARBALL_CACHE"
fi
rm -rf "$STAGE"
mkdir -p "$STAGE/lib" "$STAGE/include"

CC=cc
AR=ar
# MDBX_DEBUG=0 keeps the amalgamation in release form. _GNU_SOURCE picks
# up pread/pwrite/posix_fadvise. -pthread is needed because libmdbx uses
# pthread primitives internally for the per-process lock table.
CFLAGS_BASE="-O2 -fPIC -pthread -D_GNU_SOURCE -DNDEBUG -DMDBX_DEBUG=0"
case "$TARGET_PLATFORM" in
linux-x86_64) CC=gcc ;;
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
macos-x86_64) CC=clang; CFLAGS_BASE="$CFLAGS_BASE -arch x86_64" ;;
macos-arm64)  CC=clang; CFLAGS_BASE="$CFLAGS_BASE -arch arm64"  ;;
windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell (x64). cl.exe +
    # lib.exe. The POSIX -pthread/-fPIC/_GNU_SOURCE flags don't apply on
    # Windows (mdbx.c uses the Win32 threading + file APIs there); rebuild
    # CFLAGS in MSVC form keeping only the release/debug defines. /MD =
    # dynamic msvcrt, matching picomesh.exe and the CMake-built libs.
    command -v cl >/dev/null 2>&1 || command -v cl.exe >/dev/null 2>&1 || {
        echo "windows-x86_64 requires MSVC cl on PATH (run vcvarsall x64)" >&2; exit 1; }
    CC=cl
    AR=lib
    CFLAGS_BASE="/nologo /O2 /MD /DNDEBUG /DMDBX_DEBUG=0 /D_CRT_SECURE_NO_WARNINGS"
    ;;
*) echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac

echo "==> compiling libmdbx amalgamation"
if [ "$TARGET_PLATFORM" = "windows-x86_64" ]; then
    _SRC_W=$(cygpath -w "$SRC_DIR/mdbx.c")
    _OBJ_W=$(cygpath -w "$WORK_DIR/mdbx.obj")
    _OUT_W=$(cygpath -w "$STAGE/lib/libmdbx.lib")
    MSYS2_ARG_CONV_EXCL='*' $CC $CFLAGS_BASE /c "$_SRC_W" "/Fo${_OBJ_W}"
    MSYS2_ARG_CONV_EXCL='*' $AR /nologo "/OUT:${_OUT_W}" "${_OBJ_W}"
else
    $CC $CFLAGS_BASE -c "$SRC_DIR/mdbx.c" -o "$WORK_DIR/mdbx.o"
    $AR rcs "$STAGE/lib/libmdbx.a" "$WORK_DIR/mdbx.o"
fi
cp "$SRC_DIR/mdbx.h" "$STAGE/include/"

{ [ -f "$STAGE/lib/libmdbx.a" ] || [ -f "$STAGE/lib/libmdbx.lib" ]; } \
    || { echo "missing libmdbx.a / libmdbx.lib" >&2; exit 1; }

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .
echo "libmdbx $VERSION ($TARGET_PLATFORM) ready"
ls -lh "$TARBALL"
