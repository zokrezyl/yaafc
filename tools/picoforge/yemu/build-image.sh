#!/usr/bin/env bash
# Assemble a bootable riscv64 alpine VM with the cross-compiled
# picomesh binary baked in. No yetty project dependency: kernel + opensbi
# are built locally via build-tools/3rdparty/{linux,opensbi}/, alpine
# minirootfs is fetched straight from alpinelinux.org.
#
# Inputs:
#   build-tools/3rdparty/opensbi/      opensbi recipe + version
#   build-tools/3rdparty/linux/        kernel recipe + version + .config
#   build-deploy/                      host-runnable picomesh tree
#                                      (built by scenarios/picoforge/deploy/stage.sh)
#   build-linux-riscv64-release/picomesh  cross-compiled binary
#
# Output ($REPO_ROOT/build-yemu-release/):
#   kernel-riscv64.bin              (from build-tools/3rdparty/linux)
#   opensbi-fw_dynamic.bin          (from build-tools/3rdparty/opensbi)
#   opensbi-fw_jump.elf             (from build-tools/3rdparty/opensbi)
#   alpine-rootfs.img               (alpine minirootfs + /opt/picoforge/)
#   cache/                          shared with $HOME/.cache/picomesh-3rdparty
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
USER_CACHE="${PICOMESH_3RDPARTY_CACHE_DIR:-$HOME/.cache/picomesh-3rdparty}"

PICOMESH_BIN="$REPO_ROOT/build-linux-riscv64-release/picomesh"
WEBAPP_BIN="$REPO_ROOT/build-linux-riscv64-release/picoforge-webapp"
for b in "$PICOMESH_BIN" "$WEBAPP_BIN"; do
    if [ ! -x "$b" ]; then
        echo "FAILED: $b missing. Build it first with:" >&2
        echo "    make -C $REPO_ROOT build-linux-riscv64-release" >&2
        exit 1
    fi
done

DEPLOY="$REPO_ROOT/build-deploy"
if [ ! -d "$DEPLOY" ]; then
    echo "FAIL: $DEPLOY missing — run:" >&2
    echo "    make -C $REPO_ROOT build-deploy" >&2
    exit 1
fi
for f in picoforge.yaml run.sh frontend/static/style.css \
         init service/common.sh service/finish \
         service/mesh/run service/webapp/run service/probe/run; do
    [ -e "$DEPLOY/$f" ] || { echo "FAIL: $DEPLOY/$f missing" >&2; exit 1; }
done

mkdir -p "$BUILD" "$WORK" "$THIRD_PARTY_OUT" "$THIRD_PARTY_CACHE" "$USER_CACHE"

#-----------------------------------------------------------------------
# Kernel + opensbi (noarch RISC-V) — download the prebuilt tarball
# published by .github/workflows/build-3rdparty-<name>.yml and attached to
# the `lib-<name>-<version>` release, extract into 3rdparty/<name>/, and
# stamp with the version so we skip on subsequent runs. Falls back to a
# from-source _build.sh build only on a download miss — release not cut
# yet, offline, or PICOMESH_3RDPARTY_FORCE_BUILD set.
#
# URL base mirrors 3rdparty-fetch.cmake's PICOMESH_3RDPARTY_URL_BASE.
#-----------------------------------------------------------------------

URL_BASE="${PICOMESH_3RDPARTY_URL_BASE:-https://github.com/zokrezyl/picomesh/releases/download}"

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
        # Prefer the prebuilt release asset; build from source only on a
        # miss. --retry (without --retry-all-errors) rides out transient
        # codeload 5xx blips but fails fast on a genuine 404, so the
        # fallback kicks in promptly when no release exists.
        local url="$URL_BASE/lib-$name-$version/$name-$version.tar.gz"
        if [ -z "${PICOMESH_3RDPARTY_FORCE_BUILD:-}" ] && \
           curl -fL --retry 5 --retry-delay 3 -o "$tarball.part" "$url"; then
            mv "$tarball.part" "$tarball"
            echo "==> downloaded $name @${version}"
        else
            rm -f "$tarball.part"
            echo "==> building $name @${version} (no prebuilt at $url)"
            OUTPUT_DIR="$THIRD_PARTY_CACHE" \
            CACHE_DIR="$USER_CACHE" \
            WORK_DIR="$WORK/3rdparty-build-$name" \
                bash "$recipe_dir/_build.sh"
        fi
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

#-----------------------------------------------------------------------
# runit — the service supervisor. The minirootfs ships only busybox (no
# runsvdir/runsv), so we pull the runit package straight from the same
# Alpine CDN and extract its binaries into the rootfs below. runit lives
# in `community`; its only dependency is musl libc, already in the rootfs.
# Version is auto-resolved from the community APKINDEX so a CDN bump keeps
# working; override with RUNIT_VERSION=2.3.0-r0 to pin.
#-----------------------------------------------------------------------

RUNIT_REPO="community"
RUNIT_BASE="https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_BRANCH}/${RUNIT_REPO}/riscv64"
RUNIT_VERSION="${RUNIT_VERSION:-}"
if [ -z "$RUNIT_VERSION" ]; then
    echo "==> resolving runit version from ${RUNIT_REPO} APKINDEX"
    RUNIT_IDX="$USER_CACHE/APKINDEX-${RUNIT_REPO}-${ALPINE_BRANCH}-riscv64.tar.gz"
    curl -fL --retry 5 --retry-delay 3 -o "$RUNIT_IDX.part" "$RUNIT_BASE/APKINDEX.tar.gz"
    mv "$RUNIT_IDX.part" "$RUNIT_IDX"
    # Extract to a file first: piping tar → awk and exiting awk at the first
    # match closes the pipe early, killing tar with SIGPIPE (rc 141) under
    # `set -o pipefail`. A plain file read sidesteps that.
    tar -xzOf "$RUNIT_IDX" APKINDEX > "$WORK/APKINDEX-${RUNIT_REPO}.txt" 2>/dev/null
    RUNIT_VERSION="$(awk '/^P:runit$/{f=1} f&&/^V:/{print substr($0,3); exit}' \
        "$WORK/APKINDEX-${RUNIT_REPO}.txt")"
    [ -n "$RUNIT_VERSION" ] || { echo "FAIL: runit not in ${RUNIT_REPO} APKINDEX" >&2; exit 1; }
fi
RUNIT_APK="$USER_CACHE/runit-${RUNIT_VERSION}.apk"
if [ ! -f "$RUNIT_APK" ]; then
    echo "==> downloading runit ${RUNIT_VERSION} (${RUNIT_REPO})"
    curl -fL --retry 8 --retry-delay 5 -o "$RUNIT_APK.part" "$RUNIT_BASE/runit-${RUNIT_VERSION}.apk"
    mv "$RUNIT_APK.part" "$RUNIT_APK"
fi

# Size: 256 MiB by default — plenty for picomesh's static binary + sqlite/
# mdbx working set. Override with DISK_MIB=512 etc.
TARGET_MIB="${DISK_MIB:-256}"
ROOTFS_IMG="$BUILD/alpine-rootfs.img"

# Fresh image every run (we rsync the minirootfs in below). Cheap.
echo "==> creating ${TARGET_MIB} MiB ext4 image at $ROOTFS_IMG"
rm -f "$ROOTFS_IMG"
truncate -s "${TARGET_MIB}M" "$ROOTFS_IMG"
mkfs.ext4 -q -L picomesh-rootfs "$ROOTFS_IMG"

#-----------------------------------------------------------------------
# Mount the empty image, unpack the alpine minirootfs into it, then
# inject the picomesh payload. All sudo'd because losetup/mount require
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

# Extract runit's binaries into the rootfs. Member selection skips the apk's
# .PKGINFO/.SIGN control dotfiles (they don't match usr/ or etc/); the
# extended-header warnings GNU tar prints for apk checksums are harmless.
echo "==> installing runit ($(basename "$RUNIT_APK")) into rootfs"
sudo tar -xzf "$RUNIT_APK" -C "$MNT" usr etc/service etc/sv 2>/dev/null
[ -x "$MNT/usr/sbin/runsvdir" ] || { echo "FAIL: runsvdir missing after runit extract" >&2; exit 1; }

# /init is the runtime PID 1: the kernel cmdline (run-vm.sh) is
# `init=/opt/picoforge/init`, so this /init is a fall-back that simply chains
# into the real one — kept because /init is the alpine minirootfs convention.
echo "==> installing fallback /init → /opt/picoforge/init"
sudo tee "$MNT/init" >/dev/null <<'INIT_EOF'
#!/bin/sh
exec /opt/picoforge/init
INIT_EOF
sudo chmod 755 "$MNT/init"

#-----------------------------------------------------------------------
# Inject the picomesh payload — same shape as before, only the source
# (build-deploy/) and the binary swap (riscv64) are unchanged.
#-----------------------------------------------------------------------

echo "==> staging build-deploy/ → /opt/picoforge/"
sudo install -d -m 755 "$MNT/opt/picoforge"
sudo cp -a "$DEPLOY/picoforge.yaml" "$MNT/opt/picoforge/picoforge.yaml"
sudo cp -a "$DEPLOY/frontend"   "$MNT/opt/picoforge/frontend"
sudo cp -a "$DEPLOY/run.sh"     "$MNT/opt/picoforge/run.sh"
sudo cp -a "$DEPLOY/init"       "$MNT/opt/picoforge/init"
sudo cp -a "$DEPLOY/service"    "$MNT/opt/picoforge/service"
sudo chmod 755 "$MNT/opt/picoforge/run.sh" "$MNT/opt/picoforge/init" \
    "$MNT/opt/picoforge/service/mesh/run" \
    "$MNT/opt/picoforge/service/webapp/run" \
    "$MNT/opt/picoforge/service/probe/run" \
    "$MNT/opt/picoforge/service/finish"

# NOTE: runit's service directory is NOT wired here on the rootfs. /init builds
# the runsvdir tree on the tmpfs at boot (/tmp/service/<svc>/run -> the payload
# scripts), because runsv must create a writable supervise/ dir inside each
# service dir and the rootfs is not reliably writable (read-only under
# tinyemu/wasm, shrunk for the web demo). The supervised services are:
#   mesh    — the picomesh node (gateway + every backend, collocated)
#   webapp  — the picoforge web app (HTML page tier)
#   probe   — availability checker (1s loop, prints to the console)

echo "==> swapping in riscv64 picomesh binary → /opt/picoforge/picomesh"
sudo install -m 755 "$PICOMESH_BIN" "$MNT/opt/picoforge/picomesh"
echo "==> swapping in riscv64 picoforge-webapp → /opt/picoforge/picoforge-webapp"
sudo install -m 755 "$WEBAPP_BIN" "$MNT/opt/picoforge/picoforge-webapp"

# Symlink so PATH-based `picomesh ...` invocations still work for users
# inside the VM (e.g. after typing `picomesh serve` at a shell prompt).
sudo install -d -m 755 "$MNT/usr/local/bin"
sudo ln -sf /opt/picoforge/picomesh "$MNT/usr/local/bin/picomesh"

sync
sudo umount "$MNT"
sudo losetup -d "$LOOP"
LOOP=""

echo
echo "Build ready under $BUILD/:"
ls -lh "$BUILD/" | grep -vE "^(d|total)" | head -10
