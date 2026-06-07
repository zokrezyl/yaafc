# Shared environment + helpers for the picoforge service scripts. Sourced by
# each runit run-script (service/<svc>/run) and by the host launcher (run.sh).
# Not executable on its own — no shebang, it is always `.`-sourced.
#
# PF_ROOT is the directory that holds picomesh, picoforge-webapp,
# picoforge.yaml and frontend/. In the VM that is /opt/picoforge; on the host
# it is build-deploy/. Each run-script resolves it the same way (two levels up
# from service/<svc>/run) and exports it before sourcing us.

# Binaries + config (relative to PF_ROOT).
PICOMESH="${PF_ROOT}/picomesh"
WEBAPP="${PF_ROOT}/picoforge-webapp"
CFG="${PF_ROOT}/picoforge.yaml"
STATIC_DIR="${PF_ROOT}/frontend/static"

# Ports. mesh = the picomesh node (gateway + collocated backends);
# webapp = the browser-facing HTML page tier sourcing data from the mesh.
MESH_HOST="${MESH_HOST:-0.0.0.0}"
MESH_PORT="${MESH_PORT:-8080}"
WEBAPP_HTTP_HOST="${WEBAPP_HTTP_HOST:-0.0.0.0}"
WEBAPP_HTTP_PORT="${WEBAPP_HTTP_PORT:-8081}"

# Debug tracing ON by default: `info` level (info<warn<error) shows the whole
# request flow — gateway dispatch, accounts_register, token_issuer login,
# session start, `yhttp POST /login`, plus every warn/error — so the console
# reveals WHERE login dies / the mesh crashes. We deliberately do NOT use
# `trace`/`debug`: the probe's 1s /_describe poll emits hundreds of per-slot
# `debug` lines that would drown the request flow. The mesh node's env is
# inherited by every collocated service + the webapp.
#   YTRACE_LOG_LEVEL=trace  → everything (very noisy, full per-call detail)
#   YTRACE_LOG_LEVEL=error  → errors only (production)
#   YTRACE_DEFAULT_ON=no    → tracing off
export YTRACE_DEFAULT_ON="${YTRACE_DEFAULT_ON:-yes}"
export YTRACE_LOG_LEVEL="${YTRACE_LOG_LEVEL:-info}"

# The gateway mints HS256 JWTs signed with this secret; without it
# register/login fail. A public-demo default — the VM's /init already
# exports it, this covers host runs and is harmless when already set.
export PICOMESH_JWT_SECRET="${PICOMESH_JWT_SECRET:-picoforge-demo-jwt-secret}"

# Per-service runtime state. Under the VM /tmp is a tmpfs mounted by
# /opt/picoforge/init; on the host it is a normal dir. mkdir is idempotent.
PF_STATE="${PF_STATE:-/tmp/picoforge}"
mkdir -p "${PF_STATE}" "${PF_STATE}/repos" 2>/dev/null || true

say() { echo "[$1] $2"; }

# wait_http URL TIMEOUT_SEC — block until URL answers (any HTTP status that
# busybox wget treats as success, i.e. 2xx), or TIMEOUT_SEC elapses. Both the
# mesh (:8080) and webapp (:8081) speak HTTP, so this doubles as a port-up
# check. Returns 0 when it answers, 1 on timeout. busybox wget is the only
# HTTP client guaranteed present in the alpine minirootfs.
wait_http() {
    wh_url="$1"; wh_timeout="$2"; wh_i=0
    while [ "${wh_i}" -lt "${wh_timeout}" ]; do
        if wget -q -O /dev/null -T 2 "${wh_url}" 2>/dev/null; then
            return 0
        fi
        wh_i=$((wh_i + 1))
        sleep 1
    done
    return 1
}
