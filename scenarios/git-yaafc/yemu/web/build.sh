#!/usr/bin/env bash
# Build yaafc-yemu.{js,wasm} via emscripten, then stage the VM assets
# (kernel + opensbi + alpine-rootfs) so the browser can fetch them.
#
# Prereqs:
#   - Emscripten SDK on PATH (`source $HOME/.local/emsdk/emsdk_env.sh`).
#   - ../build/ already populated by `../build-image.sh` (gives us the
#     kernel + opensbi + rootfs).
#
# Output (`./build/`):
#   index.html              loader UI
#   serve.py                dev HTTP server
#   yaafc.cfg               tinyemu VM config (MEMFS paths)
#   yaafc-yemu.js           emscripten loader
#   yaafc-yemu.wasm         compiled tinyemu + slirp + temu
#   assets/
#     kernel-riscv64.bin
#     opensbi-fw_dynamic.bin
#     alpine-rootfs.img

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO cmd: $BASH_COMMAND" >&2' ERR

HERE="$(cd "$(dirname "$0")" && pwd)"
YEMU_BUILD="$HERE/../build"

# Prefer the operator's emsdk install over any system emscripten —
# yetty's install-emscripten.sh drops one at ~/.local/emsdk that's known
# to match the upstream LLVM/wasm-ld bundling. Distro packages (notably
# the nix-packaged emscripten) have shipped with wasm-ld out of sync
# with emcc, producing `unknown argument: --no-stack-first` at link
# time — the emsdk install is the safe path.
EMSDK="${EMSDK:-$HOME/.local/emsdk}"
if [ -x "$EMSDK/upstream/emscripten/emcc" ]; then
    export PATH="$EMSDK/upstream/emscripten:$EMSDK:$PATH"
fi
if ! command -v emcmake >/dev/null 2>&1; then
    echo "FAIL: emcmake not on PATH — install emsdk and source emsdk_env.sh" >&2
    echo "  see ../../../../README.md for the install one-liner" >&2
    exit 1
fi

for asset in kernel-riscv64.bin alpine-rootfs.img; do
    if [ ! -f "$YEMU_BUILD/$asset" ]; then
        echo "FAIL: missing $YEMU_BUILD/$asset" >&2
        echo "  run scenarios/git-yaafc/yemu/build-image.sh first." >&2
        exit 1
    fi
done

# tinyemu has no UART. The QEMU-targeted opensbi-fw_dynamic.bin emits via
# uart8250 which goes into the void here. We use the sibling yetty
# project's opensbi-fw_jump.elf which is built for tinyemu's virt
# machine (htif/SBI-console) and produces actual output through the
# bridge_console_write path.
YETTY_OPENSBI="$HERE/../../../../../yetty/build-webasm-ytrace-release/3rdparty/opensbi/opensbi-fw_jump.elf"
if [ ! -f "$YETTY_OPENSBI" ]; then
    echo "FAIL: missing yetty's tinyemu-compatible opensbi:" >&2
    echo "  $YETTY_OPENSBI" >&2
    echo "  build yetty's webasm target first (sibling repo)." >&2
    exit 1
fi

mkdir -p "$HERE/build"

echo "==> emcmake configure"
( cd "$HERE" && emcmake cmake -B build -S . -DCMAKE_BUILD_TYPE=Release )

echo "==> emmake build"
( cd "$HERE/build" && cmake --build . --parallel )

echo "==> staging VM assets under build/assets/"
mkdir -p "$HERE/build/assets"
cp -v "$YEMU_BUILD/kernel-riscv64.bin" "$HERE/build/assets/"
cp -v "$YETTY_OPENSBI"                 "$HERE/build/assets/opensbi-fw_jump.elf"
cp -v "$YEMU_BUILD/alpine-rootfs.img"  "$HERE/build/assets/"

echo
echo "Build ready under $HERE/build/:"
ls -lh "$HERE/build/yaafc-yemu.js" "$HERE/build/yaafc-yemu.wasm" "$HERE/build/assets/"
echo
echo "Serve locally:"
echo "  python3 $HERE/build/serve.py 8000 $HERE/build"
echo "  open http://127.0.0.1:8000/"
