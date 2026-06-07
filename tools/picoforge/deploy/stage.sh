#!/usr/bin/env bash
# Stage build-deploy/ — the single source of truth for everything picomesh
# needs to run (binary, config, frontend assets, launcher). The same
# directory is:
#   - run locally for host-side testing:  cd build-deploy && ./run.sh
#   - copied into the riscv64 VM rootfs by tools/picoforge/yemu/
#     build-image.sh (which swaps the host x86_64 picomesh for the
#     cross-compiled riscv64 one).
#
# Source inputs (in repo):
#   assets/picoforge/config/picoforge.yaml         mesh services config
#   assets/picoforge/static/   CSS + html + js
#   tools/picoforge/deploy/run.sh.tmpl portable launcher template
#
# Build inputs:
#   build-desktop-release/picomesh            host binary (host test only)
#
# Outputs in build-deploy/:
#   picomesh, picoforge.yaml, run.sh, frontend/static/*
#
# Run from repo root.

set -Eeuo pipefail

# This script lives at tools/picoforge/deploy/stage.sh; the repo
# root is three levels up.
REPO_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DEPLOY="$REPO_ROOT/build-deploy"

PICOMESH_HOST="$REPO_ROOT/build-desktop-release/picomesh"
# The browser-facing HTML page tier. Runs alongside the collocated gateway in
# the VM (gateway :8080, webapp :8081) and serves every GET page + static asset,
# sourcing data from the gateway over /_rpc.
WEBAPP_HOST="$REPO_ROOT/build-desktop-release/picoforge-webapp"
# The deploy (qemu / wasm) runs the COLLOCATED all-in-one config: one
# process hosts the gateway + every service + storage, calling each other
# in-process. Inside the RISC-V emulator that beats the 12-process mesh,
# where loopback-TCP between processes dominates. (The multi-process mesh
# lives in picoforge.yaml and is exercised by mesh-up.sh on real hardware.)
YAML_SRC="$REPO_ROOT/assets/picoforge/config/picoforge-webasm.yaml"
STATIC_SRC="$REPO_ROOT/assets/picoforge/static"
RUNSH_SRC="$REPO_ROOT/tools/picoforge/deploy/run.sh.tmpl"
# PID-1 init + the runit service tree (mesh / webapp / probe). These are the
# single source of truth for how each service is launched; the VM supervises
# them via runit, the host run.sh runs them as plain background children.
INIT_SRC="$REPO_ROOT/tools/picoforge/deploy/init"
SERVICE_SRC="$REPO_ROOT/tools/picoforge/deploy/service"

for b in "$PICOMESH_HOST" "$WEBAPP_HOST"; do
    if [ ! -x "$b" ]; then
        echo "FAIL: host binary missing: $b" >&2
        echo "  run: make build-desktop-release" >&2
        exit 1
    fi
done
for f in "$YAML_SRC" "$STATIC_SRC" "$RUNSH_SRC" "$INIT_SRC" "$SERVICE_SRC"; do
    [ -e "$f" ] || { echo "FAIL: missing $f" >&2; exit 1; }
done

rm -rf "$DEPLOY.new"
mkdir -p "$DEPLOY.new/frontend"

cp -a "$PICOMESH_HOST"  "$DEPLOY.new/picomesh"
cp -a "$WEBAPP_HOST"    "$DEPLOY.new/picoforge-webapp"
cp -a "$YAML_SRC"    "$DEPLOY.new/picoforge.yaml"
cp -a "$STATIC_SRC"  "$DEPLOY.new/frontend/static"
cp -a "$RUNSH_SRC"   "$DEPLOY.new/run.sh"
cp -a "$INIT_SRC"    "$DEPLOY.new/init"
cp -a "$SERVICE_SRC" "$DEPLOY.new/service"
chmod +x "$DEPLOY.new/run.sh" "$DEPLOY.new/init" \
    "$DEPLOY.new/service/mesh/run" "$DEPLOY.new/service/webapp/run" \
    "$DEPLOY.new/service/probe/run" "$DEPLOY.new/service/finish"

# Adjust the yaml's static_dir to the deploy-relative path. The source
# yaml uses an in-repo path (assets/picoforge/static) so
# mesh-up.sh from the repo root works; the deploy layout puts the
# frontend at ./frontend/static (picomesh's cwd is the deploy dir).
sed -i 's|static_dir: assets/picoforge/static|static_dir: ./frontend/static|' \
    "$DEPLOY.new/picoforge.yaml"

# Atomic swap so a partial stage never leaves a broken build-deploy/.
rm -rf "$DEPLOY"
mv "$DEPLOY.new" "$DEPLOY"

echo "build-deploy/ staged:"
ls -lh "$DEPLOY"
echo
echo "Run locally:    cd build-deploy && ./run.sh"
echo "Bake into VM:   make build-yemu-release  (then build-webasm-yemu-release)"
