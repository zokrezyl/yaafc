#!/usr/bin/env bash
# Stage build-deploy/ — the single source of truth for everything yaafc
# needs to run (binary, config, frontend assets, launcher). The same
# directory is:
#   - run locally for host-side testing:  cd build-deploy && ./run.sh
#   - copied into the riscv64 VM rootfs by scenarios/git-yaafc/yemu/
#     build-image.sh (which swaps the host x86_64 yaafc for the
#     cross-compiled riscv64 one).
#
# Source inputs (in repo):
#   scenarios/git-yaafc/yaafc.yaml         mesh services config
#   scenarios/git-yaafc/frontend/static/   CSS + html + js
#   scenarios/git-yaafc/deploy/run.sh.tmpl portable launcher template
#
# Build inputs:
#   build-desktop-release/yaafc            host binary (host test only)
#
# Outputs in build-deploy/:
#   yaafc, yaafc.yaml, run.sh, frontend/static/*
#
# Run from repo root.

set -Eeuo pipefail

# This script lives at scenarios/git-yaafc/deploy/stage.sh; the repo
# root is three levels up.
REPO_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DEPLOY="$REPO_ROOT/build-deploy"

YAAFC_HOST="$REPO_ROOT/build-desktop-release/yaafc"
YAML_SRC="$REPO_ROOT/scenarios/git-yaafc/yaafc.yaml"
STATIC_SRC="$REPO_ROOT/scenarios/git-yaafc/frontend/static"
RUNSH_SRC="$REPO_ROOT/scenarios/git-yaafc/deploy/run.sh.tmpl"

if [ ! -x "$YAAFC_HOST" ]; then
    echo "FAIL: host yaafc binary missing: $YAAFC_HOST" >&2
    echo "  run: make build-desktop-release" >&2
    exit 1
fi
for f in "$YAML_SRC" "$STATIC_SRC" "$RUNSH_SRC"; do
    [ -e "$f" ] || { echo "FAIL: missing $f" >&2; exit 1; }
done

rm -rf "$DEPLOY.new"
mkdir -p "$DEPLOY.new/frontend"

cp -a "$YAAFC_HOST"  "$DEPLOY.new/yaafc"
cp -a "$YAML_SRC"    "$DEPLOY.new/yaafc.yaml"
cp -a "$STATIC_SRC"  "$DEPLOY.new/frontend/static"
cp -a "$RUNSH_SRC"   "$DEPLOY.new/run.sh"
chmod +x "$DEPLOY.new/run.sh"

# Adjust the yaml's static_dir to the deploy-relative path. The source
# yaml uses an in-repo path (scenarios/git-yaafc/frontend/static) so
# mesh-up.sh from the repo root works; the deploy layout puts the
# frontend at ./frontend/static (yaafc's cwd is the deploy dir).
sed -i 's|static_dir: scenarios/git-yaafc/frontend/static|static_dir: ./frontend/static|' \
    "$DEPLOY.new/yaafc.yaml"

# Atomic swap so a partial stage never leaves a broken build-deploy/.
rm -rf "$DEPLOY"
mv "$DEPLOY.new" "$DEPLOY"

echo "build-deploy/ staged:"
ls -lh "$DEPLOY"
echo
echo "Run locally:    cd build-deploy && ./run.sh"
echo "Bake into VM:   make build-yemu-release  (then build-webasm-yemu-release)"
