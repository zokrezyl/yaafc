#!/usr/bin/env bash
# Assemble a bootable riscv64 alpine VM with the cross-compiled
# yaafc + yaafc-frontend binaries baked in. Mirrors yaapp/tmp/yemu/
# but yaafc is fully static (no python venv, no uv) so the image
# only needs alpine's base userland — nothing to provision at first
# boot.
#
# Inputs (downloaded into ./cache/):
#   yetty alpine-disk → small minirootfs ext4 image (~2.7 MB tarball)
#   yetty opensbi     → fw_dynamic.bin
#   yetty linux       → kernel-riscv64.bin (9p/virtio/ext4 built-in)
#
# Built locally (host):
#   build-linux-riscv64-release/yaafc           static riscv64 ELF
#   build-linux-riscv64-release/yaafc-frontend  static riscv64 ELF
#
# Outputs (under ./build/):
#   alpine-rootfs.img        ext4 disk grown to ${DISK_MIB:-256} MiB with
#                            /usr/local/bin/yaafc + yaafc-frontend +
#                            /opt/git-yaafc/ (scenario yaml, frontend
#                            static dir) injected
#   kernel-riscv64.bin       yetty kernel
#   opensbi-fw_dynamic.bin   yetty opensbi
#
# Needs: curl, brotli, tar, e2fsprogs, util-linux (losetup/mount),
# passwordless sudo.

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO cmd: $BASH_COMMAND" >&2' ERR

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$HERE/../../.." && pwd)"
CACHE="$HERE/cache"
BUILD="$HERE/build"
WORK="$HERE/tmp"

ALPINE_VER="${ALPINE_VER:-3.23.4-riscv64-1}"
OPENSBI_VER="${OPENSBI_VER:-1.4-1}"
LINUX_VER="${LINUX_VER:-7.0-1}"

URL_BASE="${YETTY_3RDPARTY_URL_BASE:-https://github.com/zokrezyl/yetty/releases/download}"
ALPINE_URL="$URL_BASE/lib-alpine-disk-${ALPINE_VER}/alpine-disk-${ALPINE_VER}.tar.gz"
OPENSBI_URL="$URL_BASE/lib-opensbi-${OPENSBI_VER}/opensbi-${OPENSBI_VER}.tar.gz"
LINUX_URL="$URL_BASE/lib-linux-${LINUX_VER}/linux-${LINUX_VER}.tar.gz"

YAAFC_BIN="$REPO_ROOT/build-linux-riscv64-release/yaafc"
YAAFC_FRONTEND_BIN="$REPO_ROOT/build-linux-riscv64-release/yaafc-frontend"

if [ ! -x "$YAAFC_BIN" ] || [ ! -x "$YAAFC_FRONTEND_BIN" ]; then
    echo "FAILED: yaafc riscv64 binaries missing. Build them first with:" >&2
    echo "    make -C $REPO_ROOT build-linux-riscv64-release" >&2
    exit 1
fi

mkdir -p "$CACHE" "$BUILD" "$WORK"

fetch() {
    local url="$1" cache="$2" descr="$3"
    [ -f "$cache" ] && return 0
    echo "==> downloading $descr"
    local part="$cache.part.$$"
    curl -fL --retry 8 --retry-delay 5 -o "$part" "$url"
    mv "$part" "$cache"
}

ALPINE_TGZ="$CACHE/alpine-disk-${ALPINE_VER}.tar.gz"
OPENSBI_TGZ="$CACHE/opensbi-${OPENSBI_VER}.tar.gz"
LINUX_TGZ="$CACHE/linux-${LINUX_VER}.tar.gz"

fetch "$ALPINE_URL"  "$ALPINE_TGZ"  "yetty alpine-disk ${ALPINE_VER}"
fetch "$OPENSBI_URL" "$OPENSBI_TGZ" "yetty opensbi ${OPENSBI_VER}"
fetch "$LINUX_URL"   "$LINUX_TGZ"   "yetty linux ${LINUX_VER}"

EXTRACT="$WORK/extract"
rm -rf "$EXTRACT"
mkdir -p "$EXTRACT/alpine" "$EXTRACT/opensbi" "$EXTRACT/linux"
tar -C "$EXTRACT/alpine"  -xzf "$ALPINE_TGZ"
tar -C "$EXTRACT/opensbi" -xzf "$OPENSBI_TGZ"
tar -C "$EXTRACT/linux"   -xzf "$LINUX_TGZ"

decompress() {
    local src="$1" dst="$2"
    [ -f "$src" ] || { echo "missing $src" >&2; exit 1; }
    echo "==> brotli -d $(basename "$src") -> $(basename "$dst")"
    brotli -d -f -o "$dst" "$src"
}
decompress "$EXTRACT/opensbi/opensbi-fw_dynamic.bin.br" "$BUILD/opensbi-fw_dynamic.bin"
decompress "$EXTRACT/linux/kernel-riscv64.bin.br"       "$BUILD/kernel-riscv64.bin"
decompress "$EXTRACT/alpine/alpine-rootfs.img.br"       "$BUILD/alpine-rootfs.img"

# yaafc is fully static — we don't need a venv or apk add at provision
# time, so the image stays small. 256 MiB is plenty of headroom for
# the binaries + sqlite/mdbx working set.
TARGET_MIB="${DISK_MIB:-256}"
echo "==> growing alpine-rootfs.img to ${TARGET_MIB} MiB"
truncate -s "${TARGET_MIB}M" "$BUILD/alpine-rootfs.img"
e2fsck -fy "$BUILD/alpine-rootfs.img" >/dev/null 2>&1 || true
resize2fs "$BUILD/alpine-rootfs.img" 2>&1 | tail -1

#-----------------------------------------------------------------------
# Inject the binaries + scenario assets via loopback mount (sudo).
#-----------------------------------------------------------------------

MNT="$WORK/mnt"
LOOP=""
cleanup() {
    mountpoint -q "$MNT" 2>/dev/null && sudo umount "$MNT" || true
    [ -n "$LOOP" ] && sudo losetup -d "$LOOP" 2>/dev/null || true
}
trap 'cleanup; rc=$?; echo "FAILED: rc=$rc line=$LINENO cmd: $BASH_COMMAND" >&2' ERR
trap cleanup EXIT

mkdir -p "$MNT"
LOOP="$(sudo losetup -f --show "$BUILD/alpine-rootfs.img")"
sudo mount "$LOOP" "$MNT"

# tmp/deploy/ is the single source of truth: it's tested locally
# (cd tmp/deploy && ./run.sh) and copied verbatim into the rootfs
# here. yaafc binary is the only thing we swap (host x86_64 → riscv64).
DEPLOY="$REPO_ROOT/tmp/deploy"
if [ ! -d "$DEPLOY" ]; then
    echo "FAIL: $DEPLOY missing — run tmp/stage-deploy.sh first" >&2
    exit 1
fi
for f in yaafc.yaml run.sh frontend/static/style.css; do
    [ -e "$DEPLOY/$f" ] || { echo "FAIL: $DEPLOY/$f missing" >&2; exit 1; }
done

echo "==> staging tmp/deploy/ → /opt/git-yaafc/"
sudo install -d -m 755 "$MNT/opt/git-yaafc"
sudo cp -a "$DEPLOY/yaafc.yaml" "$MNT/opt/git-yaafc/yaafc.yaml"
sudo cp -a "$DEPLOY/frontend"   "$MNT/opt/git-yaafc/frontend"
sudo cp -a "$DEPLOY/run.sh"     "$MNT/opt/git-yaafc/run.sh"
sudo chmod 755 "$MNT/opt/git-yaafc/run.sh"

echo "==> swapping in riscv64 yaafc binary → /opt/git-yaafc/yaafc"
sudo install -m 755 "$YAAFC_BIN" "$MNT/opt/git-yaafc/yaafc"

# Symlink so PATH-based `yaafc ...` invocations still work for users
# inside the VM (e.g. after typing `yaafc serve` at a shell prompt).
sudo install -d -m 755 "$MNT/usr/local/bin"
sudo ln -sf /opt/git-yaafc/yaafc "$MNT/usr/local/bin/yaafc"

# Legacy yaafc-frontend (standalone HTML server) — kept only because
# its CLI signature is referenced by tests, not used by the demo.
sudo install -m 755 "$YAAFC_FRONTEND_BIN" "$MNT/usr/local/bin/yaafc-frontend"

# Inline run.sh heredoc kept BELOW for archival reference only — the
# rootfs takes the file from tmp/deploy/run.sh (just installed).
# Skip the heredoc by writing it into a discarded path.
if false; then
sudo tee /dev/null >/dev/null <<'RUN_EOF'
#!/bin/sh
# git-yaafc in-VM launcher. Mirrors scenarios/git-yaafc/mesh-up.sh:
# start a parent yaafc on a control port, then trigger
# mesh_store.reconcile_from_config to spawn the full mesh — all
# backend services on their yrpc ports + the `gateway` service on
# 8080 with yhttp + the real HTML UI (/login, /register, /repos,
# /<account>/<repo>, /_rpc). Single bare yaafc on 8080 would serve
# only the gateway's /login stub; without backends, /register
# can't store accounts. Hence the bootstrap.
#
# `set -e` is OFF: a single child crashing must not knock out PID 1
# (that would kernel-panic the VM and we'd lose all diagnostics).
set -u
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

say() { echo "[run.sh] $*"; }

say "PID=$$ launcher start"

say "mount /proc /sys /dev"
mount -t proc     proc /proc 2>&1 || say "  proc mount failed (ok if already mounted)"
mount -t sysfs    sys  /sys  2>&1 || say "  sys mount failed"
mount -t devtmpfs dev  /dev  2>&1 || say "  devtmpfs mount failed (ok if already mounted)"

say "bring up net"
ip link set lo up                  2>&1 || say "  lo up failed"
ip link set eth0 up                2>&1 || say "  eth0 up failed"
ip addr add 10.0.2.15/24 dev eth0  2>&1 || say "  ip addr add failed"
ip route add default via 10.0.2.2  2>&1 || say "  ip route add failed"

mkdir -p /var/lib/git-yaafc /tmp/git-yaafc

CTRL=8800

say "spawning parent yaafc (control yhttp on 127.0.0.1:${CTRL})"
yaafc --config-file /opt/git-yaafc/yaafc.yaml \
      --frontend yhttp --host 127.0.0.1 --port ${CTRL} serve \
      </dev/null >/dev/console 2>&1 &
PARENT=$!
say "parent pid=${PARENT}"

# Wait for the control port to bind. Without ss/netstat-w we just
# loop on a wget probe with a short timeout; busybox wget is happy
# to retry a fresh TCP connect per call.
say "waiting for control port ${CTRL} to accept…"
i=0
while [ $i -lt 60 ]; do
    if wget -q -O - --timeout=1 --tries=1 "http://127.0.0.1:${CTRL}/" >/dev/null 2>&1; then
        break
    fi
    sleep 1
    i=$((i + 1))
done
say "control port up after ${i}s"

say "POST /create {class:mesh_store}"
CREATE_RESP=$(wget -q -O - \
    --header="Content-Type: application/json" \
    --post-data='{"class":"mesh_store"}' \
    "http://127.0.0.1:${CTRL}/create" 2>/dev/null || true)
say "  → ${CREATE_RESP}"
H=$(echo "${CREATE_RESP}" | sed -nE 's/.*"handle":([0-9]+).*/\1/p')
if [ -z "${H}" ]; then
    say "  FAIL: no handle in create response — gateway won't come up"
    say "  going to sleep so PID 1 stays alive for diagnostics"
    exec sleep infinity
fi
say "  mesh_store handle=${H}"

say "POST /invoke mesh_store_reconcile_from_config"
INVOKE_BODY='{"method":"mesh_store_reconcile_from_config","handle":'${H}',"args":[]}'
INVOKE_RESP=$(wget -q -O - \
    --header="Content-Type: application/json" \
    --post-data="${INVOKE_BODY}" \
    "http://127.0.0.1:${CTRL}/invoke" 2>/dev/null || true)
say "  → ${INVOKE_RESP}"
SPAWNED=$(echo "${INVOKE_RESP}" | sed -nE 's/.*"result":([0-9]+).*/\1/p')
say "  spawned=${SPAWNED:-?} mesh children"

say "waiting 3s for children to bind their yrpc/yhttp ports"
sleep 3

say "post-launch sockets (netstat -ltn):"
netstat -ltn 2>&1 || say "  netstat unavailable"

say "mesh up — parent yaafc on :${CTRL}, gateway on :8080. wait \$PARENT"
wait ${PARENT}
say "parent yaafc exited rc=$?"
say "sleeping forever to keep PID 1 alive"
exec sleep infinity
RUN_EOF
fi  # close `if false` from above — the heredoc above is dead code

sync
sudo umount "$MNT"
sudo losetup -d "$LOOP"
LOOP=""

rm -rf "$EXTRACT"

echo
echo "Build ready under $BUILD/:"
ls -lh "$BUILD/"
