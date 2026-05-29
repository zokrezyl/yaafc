#!/bin/bash
# Build OpenSBI RISC-V firmware and produce opensbi-${VERSION}.tar.gz
# containing both:
#   lib/opensbi-fw_dynamic.bin   raw firmware blob (qemu -bios consumer)
#   lib/opensbi-fw_jump.elf      ELF with htif/SBI-console — what tinyemu
#                                wasm needs because tinyemu has no UART;
#                                the QEMU-targeted fw_dynamic.bin emits via
#                                uart8250 → goes into the void under wasm.
#
# Env vars:
#   VERSION         derived — read from ./version, used in output filename
#   OUTPUT_DIR      required — where to place the tarball
#   WORK_DIR        optional — intermediate build tree
#   CROSS_COMPILE   optional — toolchain prefix (default: riscv64-linux-gnu-)
#
# The `version` file format is <upstream>-<pkg-rev>, e.g. `1.4-1` — single
# source of truth for both the upstream OpenSBI tag fetched here AND the
# output tarball name. Bump <pkg-rev> for packaging-only changes; bump
# <upstream> when moving to a new OpenSBI release.
#
# Hermetic: needs make, curl, tar, and the RISC-V cross-toolchain. No
# host-arch-specific output — the firmware is RISC-V regardless of where
# this script runs, so OUTPUT_DIR doesn't include a platform tag.

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO cmd: $BASH_COMMAND" >&2' ERR

VERSION_FILE="$(dirname "$0")/version"
[ -f "$VERSION_FILE" ] || { echo "missing $VERSION_FILE" >&2; exit 1; }
VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
[ -n "$VERSION" ] || { echo "$VERSION_FILE is empty" >&2; exit 1; }
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

# <upstream>-<pkg-rev>
OPENSBI_VERSION="${VERSION%-*}"
PKG_REV="${VERSION##*-}"
[ "$OPENSBI_VERSION" != "$VERSION" ] && [ -n "$PKG_REV" ] || {
    echo "$VERSION_FILE: expected <upstream>-<rev>, got '$VERSION'" >&2
    exit 1
}

WORK_DIR="${WORK_DIR:-/tmp/yaafc-3rdparty-opensbi}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yaafc-3rdparty}"
CROSS_COMPILE="${CROSS_COMPILE:-riscv64-linux-gnu-}"
NCPU="$(nproc 2>/dev/null || echo 4)"

SRC_URL="https://github.com/riscv-software-src/opensbi/archive/refs/tags/v${OPENSBI_VERSION}.tar.gz"
SRC_TARBALL="$CACHE_DIR/opensbi-${OPENSBI_VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

if [ ! -f "$SRC_TARBALL" ]; then
    echo "==> downloading OpenSBI ${OPENSBI_VERSION}"
    curl -fL --retry 8 --retry-delay 5 -o "$SRC_TARBALL.part" "$SRC_URL"
    mv "$SRC_TARBALL.part" "$SRC_TARBALL"
fi

SRC_DIR="$WORK_DIR/opensbi-${OPENSBI_VERSION}"
if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting"
    rm -rf "$WORK_DIR/.extract-$$"
    mkdir -p "$WORK_DIR/.extract-$$"
    tar -C "$WORK_DIR/.extract-$$" -xzf "$SRC_TARBALL"
    mv "$WORK_DIR/.extract-$$/opensbi-${OPENSBI_VERSION}" "$SRC_DIR"
    rmdir "$WORK_DIR/.extract-$$"
fi

cd "$SRC_DIR"

echo "==> building (CROSS_COMPILE=${CROSS_COMPILE}, -j${NCPU})"
# -std=gnu11: OpenSBI 1.4 typedefs `bool`, which gcc 15 (default C23) rejects.
# Push it through platform-genflags-y so the Makefile's own CFLAGS aren't clobbered.
make -j"$NCPU" \
    CROSS_COMPILE="$CROSS_COMPILE" \
    PLATFORM=generic \
    FW_JUMP=y \
    platform-genflags-y="-std=gnu11"

FW_JUMP="build/platform/generic/firmware/fw_jump.elf"
FW_DYNAMIC="build/platform/generic/firmware/fw_dynamic.bin"
[ -f "$FW_JUMP" ]    || { echo "missing $FW_JUMP"    >&2; exit 1; }
[ -f "$FW_DYNAMIC" ] || { echo "missing $FW_DYNAMIC" >&2; exit 1; }

STAGE="$WORK_DIR/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/lib"

cp "$FW_JUMP"    "$STAGE/lib/opensbi-fw_jump.elf"
cp "$FW_DYNAMIC" "$STAGE/lib/opensbi-fw_dynamic.bin"

TARBALL="$OUTPUT_DIR/opensbi-${VERSION}.tar.gz"
echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "opensbi @${VERSION} ready:"
ls -lh "$TARBALL"
tar -tzf "$TARBALL"
