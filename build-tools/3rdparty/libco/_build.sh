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
# Tarball layout (consumed by build-tools/picomesh/libs/co.cmake):
#   lib/libco.a
#   include/libco.h

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO cmd: $BASH_COMMAND" >&2' ERR

: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/version")"
[ -n "$VERSION" ] || { echo "empty version file" >&2; exit 1; }

WORK_DIR="${WORK_DIR:-/tmp/picomesh-3rdparty-libco-$TARGET_PLATFORM}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/picomesh-3rdparty}"

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

# -DLIBCO_MP is REQUIRED: picomesh runs N worker threads, each driving its
# own libco coroutine scheduler concurrently. Without LIBCO_MP, libco's
# settings.h #defines `thread_local` to nothing, so its active-context
# handles (co_active_handle / co_active_buffer) become process-global and
# concurrent co_switch() across threads corrupts the shared context —
# crashing inside co_swap. LIBCO_MP makes them real thread-locals
# (__thread), giving each worker thread an independent active context.
CFLAGS_BASE="-O2 -fPIC -std=c99 -DLIBCO_MP"
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
windows-x86_64)
    # Native MSVC — caller must have vcvarsall'd the shell (x64). cl.exe +
    # lib.exe. /MD links the DYNAMIC Microsoft C runtime (msvcrt), matching
    # picomesh.exe and the CMake-built 3rdparty libs (cl's /MD default) — a
    # /MT here would clash with the /MD exe at link time. LIBCO_MP is still
    # REQUIRED (picomesh drives one coroutine scheduler per worker thread) —
    # keep /DLIBCO_MP so co_active_* stay thread-local; without it concurrent
    # co_switch() corrupts the context.
    command -v cl >/dev/null 2>&1 || command -v cl.exe >/dev/null 2>&1 || {
        echo "windows-x86_64 requires MSVC cl on PATH (run vcvarsall x64)" >&2; exit 1; }
    CC=cl
    AR=lib
    CFLAGS_BASE="/nologo /O2 /MD /DLIBCO_MP /D_CRT_SECURE_NO_WARNINGS"
    CFLAGS_EXTRA=""
    ;;
*)
    echo "unknown TARGET_PLATFORM: $TARGET_PLATFORM" >&2
    exit 1
    ;;
esac

CFLAGS="$CFLAGS_BASE $CFLAGS_EXTRA"

echo "==> compiling libco for $TARGET_PLATFORM"
if [ "$TARGET_PLATFORM" = "windows-x86_64" ]; then
    # cl/lib want Windows-form paths; cygpath converts the MSYS/Git-Bash
    # paths and MSYS2_ARG_CONV_EXCL stops the shell mangling the /flags.
    _SRC_W=$(cygpath -w "$SRC_DIR/libco.c")
    _OBJ_W=$(cygpath -w "$WORK_DIR/libco.obj")
    _OUT_W=$(cygpath -w "$INSTALL_DIR/lib/libco.lib")
    _SRC_DIR_W=$(cygpath -w "$SRC_DIR")
    MSYS2_ARG_CONV_EXCL='*' $CC $CFLAGS "/I${_SRC_DIR_W}" /c "$_SRC_W" "/Fo${_OBJ_W}"
    MSYS2_ARG_CONV_EXCL='*' $AR /nologo "/OUT:${_OUT_W}" "${_OBJ_W}"
else
    $CC $CFLAGS -I"$SRC_DIR" -c "$SRC_DIR/libco.c" -o "$WORK_DIR/libco.o"
    $AR rcs "$INSTALL_DIR/lib/libco.a" "$WORK_DIR/libco.o"
fi
cp "$SRC_DIR/libco.h" "$INSTALL_DIR/include/"

cp -a "$INSTALL_DIR/lib"     "$STAGE/"
cp -a "$INSTALL_DIR/include" "$STAGE/"
{ [ -f "$STAGE/lib/libco.a" ] || [ -f "$STAGE/lib/libco.lib" ]; } \
    || { echo "missing libco.a / libco.lib in stage" >&2; exit 1; }
[ -f "$STAGE/include/libco.h" ] || { echo "missing libco.h in stage" >&2; exit 1; }

echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "libco @${VERSION:0:8} ($TARGET_PLATFORM) ready:"
ls -lh "$TARBALL"
