#!/usr/bin/env bash
# Assemble a bootable riscv64 alpine VM with the cross-compiled
# yaafc binary baked in. No yetty project dependency: kernel + opensbi
# are built locally via build-tools/3rdparty/{linux,opensbi}/, alpine
# minirootfs is fetched straight from alpinelinux.org.
#
# Inputs:
#   build-tools/3rdparty/opensbi/      opensbi recipe + version
#   build-tools/3rdparty/linux/        kernel recipe + version + .config
#   build-deploy/                      host-runnable yaafc tree
#                                      (built by scenarios/git-yaafc/deploy/stage.sh)
#   build-linux-riscv64-release/yaafc  cross-compiled binary
#
# Output ($REPO_ROOT/build-yemu-release/):
#   kernel-riscv64.bin              (from build-tools/3rdparty/linux)
#   opensbi-fw_dynamic.bin          (from build-tools/3rdparty/opensbi)
#   opensbi-fw_jump.elf             (from build-tools/3rdparty/opensbi)
#   alpine-rootfs.img               (alpine minirootfs + /opt/git-yaafc/)
#   cache/                          shared with $HOME/.cache/yaafc-3rdparty
#   work/                           scratch (loop mount, kernel build, …)
#
# Needs: curl, tar, e2fsprogs (mkfs.ext4, resize2fs, e2fsck),
# util-linux (losetup, mount), passwordless sudo, the RISC-V cross
# toolchain (riscv64-linux-gnu-{gcc,g++,…}), and the kernel build deps
# (bc, bison, flex, libssl-dev, libelf-dev, cpio, rsync) — first run
# of the linux recipe takes ~5-15 minutes; subsequent runs cache.

set -Eeuo pipefail
trap 'rc=$?; echo "FAILED: rc=$rc line=$LINENO cmd: $BASH_COMMAND" >&2' ERR

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$HERE/../../.." && pwd)"

# All build artifacts live at repo-root under build-yemu-release/.
BUILD="$REPO_ROOT/build-yemu-release"
WORK="$BUILD/work"
THIRD_PARTY_OUT="$BUILD/3rdparty"
THIRD_PARTY_CACHE="$BUILD/3rdparty-cache"
USER_CACHE="${YAAFC_3RDPARTY_CACHE_DIR:-$HOME/.cache/yaafc-3rdparty}"

YAAFC_BIN="$REPO_ROOT/build-linux-riscv64-release/yaafc"
if [ ! -x "$YAAFC_BIN" ]; then
    echo "FAILED: $YAAFC_BIN missing. Build it first with:" >&2
    echo "    make -C $REPO_ROOT build-linux-riscv64-release" >&2
    exit 1
fi

DEPLOY="$REPO_ROOT/build-deploy"
if [ ! -d "$DEPLOY" ]; then
    echo "FAIL: $DEPLOY missing — run:" >&2
    echo "    make -C $REPO_ROOT build-deploy" >&2
    exit 1
fi
for f in yaafc.yaml run.sh frontend/static/style.css; do
    [ -e "$DEPLOY/$f" ] || { echo "FAIL: $DEPLOY/$f missing" >&2; exit 1; }
done

mkdir -p "$BUILD" "$WORK" "$THIRD_PARTY_OUT" "$THIRD_PARTY_CACHE" "$USER_CACHE"

#-----------------------------------------------------------------------
# Local kernel + opensbi recipes — same pattern as yetty's 3rdparty
# fetch: run _build.sh to produce a tarball under 3rdparty-cache/,
# extract into 3rdparty/<lib>/, stamp with the version so we skip on
# subsequent runs.
#-----------------------------------------------------------------------

fetch_recipe() {
    local name="$1"
    local recipe_dir="$REPO_ROOT/build-tools/3rdparty/$name"
    [ -d "$recipe_dir" ] || { echo "missing recipe: $recipe_dir" >&2; exit 1; }
    local version
    version="$(tr -d '[:space:]' < "$recipe_dir/version")"
    local dest="$THIRD_PARTY_OUT/$name"
    local stamp="$dest/.fetched-$version"
    if [ -f "$stamp" ]; then
        return 0
    fi
    local tarball="$THIRD_PARTY_CACHE/$name-$version.tar.gz"
    if [ ! -f "$tarball" ]; then
        echo "==> building $name @${version}"
        OUTPUT_DIR="$THIRD_PARTY_CACHE" \
        CACHE_DIR="$USER_CACHE" \
        WORK_DIR="$WORK/3rdparty-build-$name" \
            bash "$recipe_dir/_build.sh"
    fi
    rm -rf "$dest"
    mkdir -p "$dest"
    tar -C "$dest" -xzf "$tarball"
    touch "$stamp"
    echo "==> extracted $name @${version} into $dest"
}

fetch_recipe opensbi
fetch_recipe linux

# Symlink (preferred over copy: zero duplication, paths stay obvious) the
# 3rdparty/<lib>/lib/ outputs into $BUILD/ so the operator + run-vm.sh +
# web/build.sh can see kernel/opensbi paths at predictable locations.
ln -sf "$THIRD_PARTY_OUT/linux/lib/kernel-riscv64.bin"        "$BUILD/kernel-riscv64.bin"
ln -sf "$THIRD_PARTY_OUT/opensbi/lib/opensbi-fw_dynamic.bin"  "$BUILD/opensbi-fw_dynamic.bin"
ln -sf "$THIRD_PARTY_OUT/opensbi/lib/opensbi-fw_jump.elf"     "$BUILD/opensbi-fw_jump.elf"

#-----------------------------------------------------------------------
# Alpine minirootfs — straight from alpinelinux.org. We turn the
# tarball into an ext4 image at $BUILD/alpine-rootfs.img.
#-----------------------------------------------------------------------

ALPINE_RELEASE="${ALPINE_RELEASE:-3.23.4}"
ALPINE_BRANCH="${ALPINE_RELEASE%.*}"
ALPINE_TARBALL_NAME="alpine-minirootfs-${ALPINE_RELEASE}-riscv64.tar.gz"
ALPINE_URL="https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_BRANCH}/releases/riscv64/${ALPINE_TARBALL_NAME}"
ALPINE_TARBALL="$USER_CACHE/${ALPINE_TARBALL_NAME}"

if [ ! -f "$ALPINE_TARBALL" ]; then
    echo "==> downloading alpine minirootfs ${ALPINE_RELEASE}"
    curl -fL --retry 8 --retry-delay 5 -o "$ALPINE_TARBALL.part" "$ALPINE_URL"
    mv "$ALPINE_TARBALL.part" "$ALPINE_TARBALL"
fi

# Size: 256 MiB by default — plenty for yaafc's static binary + sqlite/
# mdbx working set. Override with DISK_MIB=512 etc.
TARGET_MIB="${DISK_MIB:-256}"
ROOTFS_IMG="$BUILD/alpine-rootfs.img"

# Fresh image every run (we rsync the minirootfs in below). Cheap.
echo "==> creating ${TARGET_MIB} MiB ext4 image at $ROOTFS_IMG"
rm -f "$ROOTFS_IMG"
truncate -s "${TARGET_MIB}M" "$ROOTFS_IMG"
mkfs.ext4 -q -L yaafc-rootfs "$ROOTFS_IMG"

#-----------------------------------------------------------------------
# Mount the empty image, unpack the alpine minirootfs into it, then
# inject the yaafc payload. All sudo'd because losetup/mount require
# root.
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
LOOP="$(sudo losetup -f --show "$ROOTFS_IMG")"
sudo mount "$LOOP" "$MNT"

echo "==> unpacking alpine minirootfs into image"
sudo tar -C "$MNT" -xzf "$ALPINE_TARBALL"

# Write a /init that boots straight into our /opt/git-yaafc/run.sh.
# The kernel cmdline (set in run-vm.sh) is `init=/opt/git-yaafc/run.sh`,
# so /init here is fall-back only — kept around because /init is the
# alpine minirootfs convention.
echo "==> installing fallback /init"
sudo tee "$MNT/init" >/dev/null <<'INIT_EOF'
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sys /sys
mount -t devtmpfs dev /dev 2>/dev/null || true
hostname yaafc-yemu
ip link set lo up
ip link set eth0 up                       2>/dev/null
ip addr add 10.0.2.15/24 dev eth0         2>/dev/null
ip route add default via 10.0.2.2         2>/dev/null
echo "nameserver 10.0.2.3" > /etc/resolv.conf
exec /bin/sh
INIT_EOF
sudo chmod 755 "$MNT/init"

#-----------------------------------------------------------------------
# Inject the yaafc payload — same shape as before, only the source
# (build-deploy/) and the binary swap (riscv64) are unchanged.
#-----------------------------------------------------------------------

echo "==> staging build-deploy/ → /opt/git-yaafc/"
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

sync
sudo umount "$MNT"
sudo losetup -d "$LOOP"
LOOP=""

echo
echo "Build ready under $BUILD/:"
ls -lh "$BUILD/" | grep -vE "^(d|total)" | head -10
