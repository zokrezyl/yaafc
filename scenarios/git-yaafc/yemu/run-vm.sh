#!/usr/bin/env bash
# Boot the riscv64 alpine VM built by ./build-image.sh and run yaafc.
#
# Artefacts (all under ./build/):
#   opensbi-fw_dynamic.bin    yetty opensbi
#   kernel-riscv64.bin        yetty linux (9p/virtio/ext4 built-in)
#   alpine-rootfs.img         alpine rootfs with yaafc + yaafc-frontend
#                             pre-injected by build-image.sh
#
# Host ↔ guest plumbing:
#   stdio  <—> hvc0          (interactive shell — Ctrl-A X to quit)
#   tcp    18080 -> guest 8080  (gateway HTTP)
#   tcp    18081 -> guest 8081  (HTML frontend)

set -Eeuo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="$HERE/build"

KERNEL="$BUILD/kernel-riscv64.bin"
BIOS="$BUILD/opensbi-fw_dynamic.bin"
DISK="$BUILD/alpine-rootfs.img"

for f in "$KERNEL" "$BIOS" "$DISK"; do
    [ -f "$f" ] || { echo "missing $f — run ./build-image.sh first" >&2; exit 1; }
done

QEMU="$(command -v qemu-system-riscv64)"
[ -x "$QEMU" ] || { echo "qemu-system-riscv64 not on PATH" >&2; exit 1; }

SMP="${SMP:-2}"
MEM="${MEM:-512M}"
# init=/opt/git-yaafc/run.sh runs the launcher baked into the image
# at PID 1. console=hvc0 wires the shell to our stdio chardev.
CMDLINE="console=hvc0 earlycon=sbi root=/dev/vda rw init=/opt/git-yaafc/run.sh"

echo "==> launching qemu-system-riscv64 (yaafc demo)"
echo "    kernel    $KERNEL"
echo "    bios      $BIOS"
echo "    disk      $DISK"
echo "    forwards  host 18080 -> guest 8080   (gateway HTTP)"
echo "              host 18081 -> guest 8081   (HTML frontend)"
echo "    Quit with Ctrl-A X."
echo

exec "$QEMU" \
    -machine virt -m "$MEM" -smp "$SMP" \
    -bios "$BIOS" \
    -kernel "$KERNEL" \
    -append "$CMDLINE" \
    -drive "file=$DISK,format=raw,if=none,id=hd0" \
    -device virtio-blk-device,drive=hd0 \
    -netdev user,id=net0,hostfwd=tcp::18080-:8080,hostfwd=tcp::18081-:8081 \
    -device virtio-net-device,netdev=net0 \
    -chardev stdio,id=ch0,signal=off,mux=on \
    -device virtio-serial-device \
    -device virtconsole,chardev=ch0 \
    -display none -no-reboot \
    "$@"
