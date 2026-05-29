#!/bin/bash
# Build Linux kernel for RISC-V64, producing linux-${VERSION}.tar.gz
# containing:
#   lib/kernel-riscv64.bin
#
# Alpine rootfs is handled by scenarios/git-yaafc/yemu/build-image.sh
# (downloads the upstream minirootfs and bakes the ext4 image), not by
# this recipe. The linux artifact here is just the kernel.
#
# Env vars:
#   VERSION         derived — read from ./version
#   OUTPUT_DIR      required — where to place the tarball
#   WORK_DIR        optional — intermediate build tree
#   CROSS_COMPILE   optional — toolchain prefix
#
# The `version` file format is <upstream>-<pkg-rev>, e.g. `7.0-1`. Bump
# <pkg-rev> for packaging-only changes (config tweaks); bump <upstream>
# when moving to a new kernel.
#
# Hermetic Linux build needs: make, the RISC-V cross-toolchain, bc,
# bison, flex, libssl-dev, libelf-dev, cpio, rsync, curl, tar.

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO cmd: $BASH_COMMAND" >&2' ERR

VERSION_FILE="$(dirname "$0")/version"
[ -f "$VERSION_FILE" ] || { echo "missing $VERSION_FILE" >&2; exit 1; }
VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
[ -n "$VERSION" ] || { echo "$VERSION_FILE is empty" >&2; exit 1; }
: "${OUTPUT_DIR:?OUTPUT_DIR is required}"

LINUX_VERSION="${VERSION%-*}"
PKG_REV="${VERSION##*-}"
[ "$LINUX_VERSION" != "$VERSION" ] && [ -n "$PKG_REV" ] || {
    echo "$VERSION_FILE: expected <upstream>-<rev>, got '$VERSION'" >&2
    exit 1
}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK_DIR="${WORK_DIR:-/tmp/yaafc-3rdparty-linux}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/yaafc-3rdparty}"

# Pick the right cross prefix: try the gnu one first (Ubuntu/Debian's
# `gcc-riscv64-linux-gnu`), then unknown- (Linux upstream / yetty docs
# use this), then fall back to whatever the operator passed.
if [ -z "${CROSS_COMPILE:-}" ]; then
    if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
        CROSS_COMPILE="riscv64-linux-gnu-"
    elif command -v riscv64-unknown-linux-gnu-gcc >/dev/null 2>&1; then
        CROSS_COMPILE="riscv64-unknown-linux-gnu-"
    else
        echo "FAIL: no riscv64-{linux-gnu,unknown-linux-gnu}-gcc on PATH" >&2
        exit 1
    fi
fi

KERNEL_CONFIG="$SCRIPT_DIR/linux-kernel-${LINUX_VERSION}.config"
[ -f "$KERNEL_CONFIG" ] || {
    echo "missing kernel config: $KERNEL_CONFIG" >&2; exit 1; }

NCPU="$(nproc 2>/dev/null || echo 4)"

KERNEL_URL="https://github.com/torvalds/linux/archive/refs/tags/v${LINUX_VERSION}.tar.gz"
KERNEL_TARBALL="$CACHE_DIR/linux-${LINUX_VERSION}.tar.gz"

mkdir -p "$WORK_DIR" "$OUTPUT_DIR" "$CACHE_DIR"

if [ ! -f "$KERNEL_TARBALL" ]; then
    echo "==> downloading Linux ${LINUX_VERSION} (~250 MB)"
    curl -fL --retry 8 --retry-delay 5 -o "$KERNEL_TARBALL.part" "$KERNEL_URL"
    mv "$KERNEL_TARBALL.part" "$KERNEL_TARBALL"
fi

SRC_DIR="$WORK_DIR/linux-${LINUX_VERSION}"
if [ ! -d "$SRC_DIR" ]; then
    echo "==> extracting kernel source"
    rm -rf "$WORK_DIR/.extract-$$"
    mkdir -p "$WORK_DIR/.extract-$$"
    tar -C "$WORK_DIR/.extract-$$" -xzf "$KERNEL_TARBALL"
    mv "$WORK_DIR/.extract-$$/linux-${LINUX_VERSION}" "$SRC_DIR"
    rmdir "$WORK_DIR/.extract-$$"
fi

cp "$KERNEL_CONFIG" "$SRC_DIR/.config"

echo "==> building kernel (CROSS_COMPILE=${CROSS_COMPILE}, -j${NCPU})"
make -C "$SRC_DIR" \
    ARCH=riscv CROSS_COMPILE="$CROSS_COMPILE" \
    -j"$NCPU" olddefconfig Image

KERNEL_IMAGE="$SRC_DIR/arch/riscv/boot/Image"
[ -f "$KERNEL_IMAGE" ] || { echo "missing $KERNEL_IMAGE" >&2; exit 1; }

STAGE="$WORK_DIR/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/lib"
cp "$KERNEL_IMAGE" "$STAGE/lib/kernel-riscv64.bin"

TARBALL="$OUTPUT_DIR/linux-${VERSION}.tar.gz"
echo "==> packaging -> $TARBALL"
tar -C "$STAGE" -czf "$TARBALL" .

echo ""
echo "linux @${VERSION} ready:"
ls -lh "$TARBALL"
tar -tzf "$TARBALL"
