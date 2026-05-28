#!/bin/bash
# Builds the SQLite amalgamation for $TARGET_PLATFORM as a single static
# library.
#
# Required env:
#   TARGET_PLATFORM   linux-x86_64 | linux-aarch64 | macos-arm64 | macos-x86_64
#   OUTPUT_DIR        where the tarball is written
#
# Tarball layout (consumed by build-tools/yaafc/libs/sqlite3.cmake):
#   lib/libsqlite3.a
#   include/sqlite3.h

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"

# SQLite's amalgamation URL encodes the version as <major>.<minor>00.<patch>
# but the official download page uses <YEAR>/sqlite-amalgamation-<MMNNPP>.zip
# where MMNNPP = major*1000000 + minor*10000 + patch*1000. We fetch the
# autoconf tarball form which has a stable naming scheme.
# 3.46.1 → 3460100; URL component is sqlite-autoconf-3460100.tar.gz.
IFS='.' read -r _MAJ _MIN _PAT <<< "$VERSION"
SJVER=$(printf "%d%02d%02d00" "$_MAJ" "$_MIN" "$_PAT")
URL="https://www.sqlite.org/2024/sqlite-autoconf-${SJVER}.tar.gz"
TARBALL_CACHE="$HOME/.cache/yaafc-3rdparty/sqlite-autoconf-${SJVER}.tar.gz"
WORK_DIR="${WORK_DIR:-/tmp/yaafc-3rdparty-libsqlite3-$TARGET_PLATFORM}"
SRC_DIR="$WORK_DIR/sqlite-autoconf-${SJVER}"
STAGE="$WORK_DIR/stage-${TARGET_PLATFORM}"
TARBALL="$OUTPUT_DIR/libsqlite3-${TARGET_PLATFORM}-${VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$(dirname "$TARBALL_CACHE")"

if [ ! -f "$TARBALL_CACHE" ]; then
    echo "==> downloading sqlite ${VERSION}"
    curl -fL --retry 8 --retry-delay 5 --retry-all-errors \
        -o "$TARBALL_CACHE.part" "$URL"
    mv "$TARBALL_CACHE.part" "$TARBALL_CACHE"
fi
if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting"
    tar -C "$WORK_DIR" -xzf "$TARBALL_CACHE"
fi
rm -rf "$STAGE"
mkdir -p "$STAGE/lib" "$STAGE/include"

CC=cc
AR=ar
CFLAGS_BASE="-O2 -fPIC -DSQLITE_THREADSAFE=1 -DSQLITE_ENABLE_RTREE=0 -DSQLITE_OMIT_LOAD_EXTENSION=1"
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
*) echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2; exit 1 ;;
esac

echo "==> compiling sqlite3 amalgamation"
$CC $CFLAGS_BASE -c "$SRC_DIR/sqlite3.c" -o "$WORK_DIR/sqlite3.o"
$AR rcs "$STAGE/lib/libsqlite3.a" "$WORK_DIR/sqlite3.o"
cp "$SRC_DIR/sqlite3.h" "$STAGE/include/"

[ -f "$STAGE/lib/libsqlite3.a" ] || { echo "missing libsqlite3.a" >&2; exit 1; }

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .
echo "libsqlite3 $VERSION ($TARGET_PLATFORM) ready"
ls -lh "$TARBALL"
