#!/usr/bin/env bash
# picoforge — bring up the full mesh and prove it works end-to-end.
#
# Layout:
#
#   parent picomesh --frontend yhttp --port 8800        ← control channel (REST)
#     ↳ mesh_mesh_reconcile_from_config spawns one child per service
#       in mesh.services.* on the service's own port. Each backend
#       child uses --frontend yrpc (binary, for inter-service RPC).
#       The 'gateway' service (port 8090) overrides this with
#       `frontend: yhttp` in its YAML block — it serves the HTML UI
#       that browsers / curl can hit, and it opens yrpc client
#       sessions to every backend via its `remotes:` block.
#
# Each child inherits --config-file AND --name <self> from the parent.
# The engine's service-projection step (gh#1) merges
# `mesh.services.<self>.config` onto the child's config root so plugins
# find their config at natural paths.
#
# Run from the repo root after `make build-desktop-release`.

set -uo pipefail

# Defaults — override with the options below.
CONFIG=assets/picoforge/config/picoforge.yaml
PORT_RANGE=8800-8999
NAME=

usage() {
    cat <<EOF
usage: mesh-up.sh [options]

Bring up the picoforge mesh, run the end-to-end smoke, then stay live until
Ctrl-C. The control, registry, gateway and webapp ports are allocated from FREE
ports in --range; every other service is 'port: auto' (portalloc + the mesh
allocate the backend ports themselves). Allocating instead of hardcoding means a
re-run, or a second co-resident instance, never fights over a busy port.

Options:
  -n, --name NAME      instance name → isolates ALL on-disk state under
                       /tmp/picoforge-NAME so parallel meshes never clobber
                       each other's data (default: unnamed → /tmp/picoforge)
  -r, --range LO-HI    range to allocate the 4 fixed ports from   [$PORT_RANGE]
  -c, --config FILE    mesh config file                           [$CONFIG]
  -h, --help           show this help and exit

Env:
  PICOMESH_JWT_SECRET   shared HS256 signing secret               [dev default]

The 4 fixed ports are: control (the parent this script drives), registry (the
discovery address injected into every node), gateway (the mesh HTTP API the
webapp talks to), and webapp (the browser-facing page tier). They are picked
from FREE ports in --range, so two instances do not fight over a port.

To run a SECOND co-resident mesh, give it a distinct --name (separate data
root) — the ports are already auto-picked, so a different name is enough:
    ./tools/picoforge/mesh-up.sh --name a
    ./tools/picoforge/mesh-up.sh --name b
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -n|--name)      NAME=${2:?--name needs a value};       shift 2 ;;
        -r|--range)     PORT_RANGE=${2:?--range needs LO-HI};  shift 2 ;;
        -c|--config)    CONFIG=${2:?--config needs a path};    shift 2 ;;
        -h|--help|help) usage; exit 0 ;;
        *) echo "mesh-up: unknown argument '$1'" >&2; usage >&2; exit 2 ;;
    esac
done

# Every byte of on-disk state lives under this root. A distinct --name gives a
# distinct root, so a second mesh on this host can't wipe or corrupt the first
# (the script `rm -rf`s its OWN root on bring-up — never a shared tree). The
# per-service paths in the config (sharded, rel/*, repos, nodes, mesh-state.db)
# all hang off /tmp/picoforge by default; we override each onto $ROOT below.
ROOT=/tmp/picoforge${NAME:+-$NAME}

# The script's OWN artifacts (parent/sidecar/runner logs and the curl cookie
# jars) live with the instance too, under $ROOT/logs — not a shared repo-local
# tmp/ that two named meshes would trample. Everything this run writes is
# therefore self-contained in $ROOT.
LOG_DIR=$ROOT/logs
PARENT_LOG=$LOG_DIR/mesh-parent.log
SIDECAR_LOG=$LOG_DIR/sidecar.log
RUNNER_LOG=$LOG_DIR/runner-agent.log
COOKIES=$LOG_DIR/cookies.txt
SIDE_COOKIES=$LOG_DIR/side-cookies.txt
ROOT_COOKIES=$LOG_DIR/root-cookies.txt

PORT_LO=${PORT_RANGE%-*}
PORT_HI=${PORT_RANGE#*-}

PICOMESH=./build-desktop-release/picomesh
WEBAPP=./build-desktop-release/picoforge-webapp

# Six services can't use 'port: auto' — control, registry, gateway, webapp, the
# internal bridge and the service console — so the script finds their ports
# itself, plus a dynamic range to start portalloc with. ALL of it is discovered
# by probing for FREE ports, so a plain `mesh-up.sh` with no flags just works,
# and a second mesh (started after the first has bound its ports) lands on
# different free ones automatically — no --range, no per-instance bookkeeping.
#
#   * 6 free ports anywhere in --range (8800-8999) → the fixed services.
#   * a free CONTIGUOUS block of 60 ports in 8300-8799 → portalloc's dynamic
#     range (the ports it then hands the ~22 'port: auto' backends). A block
#     instead of the shared 8300-8799 means two meshes' backends don't contend
#     on one range during concurrent bring-up.
#
# The probe binds each candidate to confirm it's free, holds it so the same port
# isn't picked twice in this pass, then releases everything before the mesh binds
# for real.
PA_LO=8300; PA_HI=8799; PA_BLOCK=60
mapfile -t _alloc < <(python3 - "$PORT_LO" "$PORT_HI" "$PA_LO" "$PA_HI" "$PA_BLOCK" <<'PY'
import socket, sys
lo, hi, pa_lo, pa_hi, block = (int(a) for a in sys.argv[1:6])
held = []
def grab(port):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.bind(("127.0.0.1", port)); held.append(s); return True
    except OSError:
        s.close(); return False
# 6 free fixed ports, scanning the whole range.
fixed, p = [], lo
while len(fixed) < 6 and p <= hi:
    if grab(p): fixed.append(p)
    p += 1
# First aligned block of `block` consecutive free ports for portalloc.
pa = None
for start in range(pa_lo, pa_hi - block + 2, block):
    tmp = []
    def grab_tmp(port):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            s.bind(("127.0.0.1", port)); tmp.append(s); return True
        except OSError:
            s.close(); return False
    if all(grab_tmp(start + i) for i in range(block)):
        pa = (start, start + block - 1); held += tmp; break
    for s in tmp:
        s.close()
for s in held:
    s.close()
if len(fixed) == 6 and pa:
    for v in fixed: print(v)
    print(pa[0]); print(pa[1])
else:
    sys.exit(1)
PY
)
[ "${#_alloc[@]}" -eq 8 ] || { echo "mesh-up: could not find 6 free ports in $PORT_RANGE plus a free portalloc block in $PA_LO-$PA_HI" >&2; exit 1; }
CTRL=${_alloc[0]}; REG=${_alloc[1]}; WEB=${_alloc[2]}; SIDE=${_alloc[3]}; BRIDGE=${_alloc[4]}; CONSOLE=${_alloc[5]}
PA_RANGE_LO=${_alloc[6]}; PA_RANGE_HI=${_alloc[7]}
DB=$ROOT/central.db
echo "mesh-up: instance '${NAME:-<unnamed>}' — data root $ROOT (logs $LOG_DIR)"
echo "mesh-up: ports — control=$CTRL registry=$REG gateway=$WEB webapp=$SIDE bridge=$BRIDGE console=$CONSOLE (free in $PORT_RANGE)"
echo "mesh-up: portalloc dynamic range — $PA_RANGE_LO-$PA_RANGE_HI (free block in $PA_LO-$PA_HI)"

# Start from a clean slate. Wipe THIS instance's ENTIRE state root ($ROOT), not
# just central.db — the backends persist accounts/sessions in the sharded mdbx
# store and the relational shards under $ROOT, so leaving it behind makes a
# re-run fail: `alice`/`bob` from the previous run still exist, so POST /register
# returns "username already taken" (HTTP 200) instead of 303, and the
# login/cookie assertions cascade. Removing the whole tree makes every
# run reproducible. We only ever wipe $ROOT — a differently-named instance has
# its own root and is untouched.
rm -rf "$ROOT"
mkdir -p "$ROOT" "$LOG_DIR"
rm -f "$DB"

PASS=0
FAIL=0
note_pass() { PASS=$((PASS+1)); echo "  PASS: $1"; }
note_fail() { FAIL=$((FAIL+1)); echo "  FAIL: $1" >&2; }

# issue #19: the mesh signs/verifies access JWTs with a shared HS256 secret
# sourced from PICOMESH_JWT_SECRET. Children inherit this env from the parent,
# so every node resolves the same key. A dev default is fine here; a real
# deployment exports its own secret before bring-up.
export PICOMESH_JWT_SECRET="${PICOMESH_JWT_SECRET:-picoforge-dev-mesh-secret-change-me}"

# Error-only tracing on by default — the control parent's env is inherited by
# every spawned backend, so yerror() across the whole mesh is captured at low
# overhead (per-thread async log buffer, no shared-sink serialization).
# Override: YTRACE_LOG_LEVEL=trace for full tracing, YTRACE_DEFAULT_ON=no to mute.
export YTRACE_DEFAULT_ON="${YTRACE_DEFAULT_ON:-yes}"
export YTRACE_LOG_LEVEL="${YTRACE_LOG_LEVEL:-error}"

echo "[1/6] starting parent picomesh (yhttp control) on :${CTRL} (registry :${REG}, gateway :${WEB})"
# Pin every on-disk path onto $ROOT. The config ships with /tmp/picoforge
# defaults (so a bare `picomesh` run still works); these overrides move ALL of
# this instance's state under its own root. The mesh's service-projection step
# merges `mesh.services.<svc>.config` onto each child, so overriding the nested
# `…config.<plugin>.path` here reaches the child plugin at its natural path.
"$PICOMESH" --config-file "$CONFIG" --frontend yhttp --host 127.0.0.1 --port "$CTRL" \
    --config "mesh.services.registry.port=$REG" \
    --config "mesh.services.gateway.port=$WEB" \
    --config "storage.db_path=$ROOT/mesh-state.db" \
    --config "mesh.nodes_dir=$ROOT/nodes" \
    --config "mesh.services.sharded_storage.config.sharded_storage.path=$ROOT/sharded" \
    --config "mesh.services.rstore_uid.config.relational_storage.path=$ROOT/rel/uid" \
    --config "mesh.services.rstore_username.config.relational_storage.path=$ROOT/rel/username" \
    --config "mesh.services.rstore_session.config.relational_storage.path=$ROOT/rel/session" \
    --config "mesh.services.rstore_token.config.relational_storage.path=$ROOT/rel/token" \
    --config "mesh.services.git_repo.config.git_repo.repos_dir=$ROOT/repos" \
    --config "mesh.services.internal_yhttp_bridge.port=$BRIDGE" \
    --config "mesh.services.service_console.port=$CONSOLE" \
    --config "mesh.services.service_console.config.alpine.upstream.port=$BRIDGE" \
    --config "mesh.services.portalloc.config.portalloc.port_range=$PA_RANGE_LO-$PA_RANGE_HI" \
    serve > $PARENT_LOG 2>&1 &
PARENT=$!
cleanup() {
    # The mesh owns its children: SIGTERM the control parent and its reaper
    # takes the spawned backends and core webapps down. Never pkill, never kill
    # a backend child directly — see CLAUDE.md "Process lifecycle & teardown".
    [ -n "${SIDECAR:-}" ] && kill -TERM "$SIDECAR" 2>/dev/null || true
    kill -TERM "$PARENT" 2>/dev/null || true
}
trap cleanup EXIT INT TERM
sleep 0.5

http_post() {
    local port=$1 path=$2 body=$3
    curl -sS --max-time 10 -XPOST -H 'Content-Type: application/json' \
         "http://127.0.0.1:$port$path" -d "$body"
}
http_form() {
    local port=$1 path=$2; shift 2
    curl -sS --max-time 10 -b $COOKIES -c $COOKIES -XPOST \
         "http://127.0.0.1:$port$path" "$@"
}
http_get() {
    local port=$1 path=$2
    curl -sS --max-time 10 -b $COOKIES -c $COOKIES \
         "http://127.0.0.1:$port$path"
}
expect_contains() {
    local label=$1 resp=$2 needle=$3
    if [[ "$resp" =~ $needle ]]; then
        note_pass "$label"
    else
        note_fail "$label — got: ${resp:0:200}"
    fi
}

echo "[2/6] create mesh_mesh on parent"
out=$(http_post "$CTRL" /create '{"class":"mesh_mesh"}')
H=$(echo "$out" | sed -E 's/.*"handle":([0-9]+).*/\1/')
expect_contains 'mesh_mesh create' "$out" '"handle":[0-9]+'

echo "[3/6] mesh.reconcile_from_config — spawn every service as a child"
out=$(http_post "$CTRL" /invoke "{\"method\":\"mesh_mesh_reconcile_from_config\",\"handle\":$H,\"args\":[]}")
expect_contains 'reconcile spawn count > 0' "$out" '"result":[1-9][0-9]*'
SPAWNED=$(echo "$out" | sed -E 's/.*"result":([0-9]+).*/\1/')
echo "  spawned: $SPAWNED children"

echo "[4/6] waiting for children to bind…"
# With `port: auto`, each node allocates its port through portalloc and
# discovers its remotes through the registry at boot, so the gateway is ready
# a few seconds after reconcile rather than instantly. Poll its /_describe
# (the gateway only answers once it has opened its backend remotes) instead of
# guessing a fixed sleep.
for _ in $(seq 1 60); do
    if curl -sS --max-time 2 -o /dev/null "http://127.0.0.1:$WEB/_describe" 2>/dev/null; then
        break
    fi
    sleep 0.5
done
sleep 0.5

echo "[5/6] gateway is API + auth-action only (NO HTML pages) on :${WEB}"
rm -f $COOKIES
touch $COOKIES

# The gateway serves NO HTML pages — every GET page now belongs to the
# picoforge webapp (tested in [5b] below). The gateway must REFUSE
# a GET HTML route. This is the architecture invariant (CLAUDE.md):
# gateway = API (/_rpc, /_describe) + the auth/action POSTs that own the
# session cookie; the webapp renders pages over /_rpc.
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' http://127.0.0.1:$WEB/login)
[ "$code" = "404" ] && note_pass "gateway refuses GET /login (404 — page is the webapp's)" \
                     || note_fail "gateway GET /login returned $code (want 404 — gateway serves no HTML)"
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' http://127.0.0.1:$WEB/)
[ "$code" = "404" ] && note_pass "gateway refuses GET / (404)" \
                     || note_fail "gateway GET / returned $code (want 404)"
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' http://127.0.0.1:$WEB/alice/website)
[ "$code" = "404" ] && note_pass "gateway refuses GET /<acct>/<repo> page (404)" \
                     || note_fail "gateway GET repo page returned $code (want 404)"

# yaapp split: /login does NOT auto-register. Cold sign-in must fail
# with "no such user" before we visit /register. (POST auth action —
# the gateway DOES own this; it mints/clears the session cookie.)
out=$(curl -sS --max-time 10 -XPOST http://127.0.0.1:$WEB/login \
           --data-urlencode 'username=alice' --data-urlencode 'password=hunter2')
expect_contains 'POST /login on unregistered user fails' "$out" 'no such user'

# The FIRST account to ever register becomes the deployment site-owner
# (admin) — the same bootstrap GitLab uses for its `root` user. Register
# it first, under the conventional name, so the deployment admin is
# `root` and not a demo user. Its session is discarded here; every flow
# below runs as alice, a regular (non-admin) user.
hdrs=$(curl -sS --max-time 10 -D - -o /dev/null \
            -XPOST http://127.0.0.1:$WEB/register \
            --data-urlencode 'username=root' --data-urlencode 'password=rootpw')
expect_contains 'POST /register (root) → 303 (site-owner bootstrap)' "$hdrs" '303 See Other'

# Register alice — the SECOND user, so a regular (non-owner) account.
# Creates the account, mints a session, redirects to /alice (GitHub-style
# namespace landing, mirroring yaapp's _landing_url).
hdrs=$(curl -sS --max-time 10 -D - -o /dev/null -c $COOKIES \
            -XPOST http://127.0.0.1:$WEB/register \
            --data-urlencode 'username=alice' --data-urlencode 'password=hunter2')
expect_contains 'POST /register → 303 /alice'   "$hdrs" '303 See Other.*[Ll]ocation: /alice|[Ll]ocation: /alice.*303'
expect_contains 'POST /register sets sid cookie' "$hdrs" 'Set-Cookie: picomesh-sid='
expect_contains 'POST /register sets uname cookie' "$hdrs" 'Set-Cookie: picomesh-uname=alice'

# A second /register for the same name must be rejected.
out=$(curl -sS --max-time 10 -XPOST http://127.0.0.1:$WEB/register \
           --data-urlencode 'username=alice' --data-urlencode 'password=hunter2')
expect_contains 'POST /register rejects duplicate' "$out" 'already taken'

# Sign-out invalidates the session server-side AND wipes cookies.
hdrs=$(curl -sS --max-time 10 -D - -o /dev/null -b $COOKIES -c $COOKIES \
            -XPOST http://127.0.0.1:$WEB/logout)
expect_contains 'POST /logout → 303 /login'        "$hdrs" '303 See Other'
expect_contains 'POST /logout clears sid cookie'   "$hdrs" 'picomesh-sid=;'
expect_contains 'POST /logout clears uname cookie' "$hdrs" 'picomesh-uname=;'

# Re-login (now that the account exists from /register).
hdrs=$(curl -sS --max-time 10 -D - -o /dev/null -c $COOKIES \
            -XPOST http://127.0.0.1:$WEB/login \
            --data-urlencode 'username=alice' --data-urlencode 'password=hunter2')
expect_contains 'POST /login → 303 /alice'   "$hdrs" '303 See Other.*[Ll]ocation: /alice|[Ll]ocation: /alice.*303'
expect_contains 'POST /login sets sid cookie' "$hdrs" 'Set-Cookie: picomesh-sid='

# Create a repo under /alice/website via /repos/new (POST `name`) — an
# authenticated action POST the gateway forwards to the git_repo backend.
hdrs=$(curl -sS --max-time 10 -D - -o /dev/null -b $COOKIES \
            -XPOST http://127.0.0.1:$WEB/repos/new \
            --data-urlencode 'name=website')
expect_contains 'POST /repos/new → 303 /alice/website' "$hdrs" '303 See Other.*/alice/website|/alice/website.*303'

# The repo's data is reachable via the gateway API (/_rpc) — this is how
# the webapp sources the page the gateway no longer renders. The gateway is
# now an auth boundary (issue #19): this read requires a signed-in session, so
# we send alice's cookie (captured at /login above).
out=$(curl -sS --max-time 10 -b $COOKIES -XPOST http://127.0.0.1:$WEB/_rpc \
           -H 'Content-Type: application/json' \
           -d '{"path":"git_repo.git_repo.count_total","args":[]}')
expect_contains 'gateway /_rpc git_repo.count_total (authed, repo data via API)' "$out" '"result":[1-9][0-9]*'

# ---- [5a] security pipeline (issue #19): authn chain + policy authorizer ----
# The browser only ever holds an OPAQUE sid — the cookie value must NOT be a
# JWT (no dot-separated base64url segments). The JWT lives server-side only.
SID_VAL=$(awk '/picomesh-sid/ {print $NF}' $COOKIES 2>/dev/null | tail -1)
if [[ -n "$SID_VAL" && "$SID_VAL" != *.* ]]; then
    note_pass "login sets an opaque sid cookie (not a JWT)"
else
    note_fail "sid cookie looks like a JWT or is empty: '${SID_VAL}'"
fi

# Anonymous call to an authenticated endpoint → 401 (no credential, policy
# requires one). It must NOT silently succeed as anonymous.
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -XPOST http://127.0.0.1:$WEB/_rpc \
            -H 'Content-Type: application/json' \
            -d '{"path":"git_repo.git_repo.count_total","args":[]}')
[ "$code" = "401" ] && note_pass "anonymous /_rpc authed endpoint → 401" \
                     || note_fail "anonymous /_rpc count_total returned $code (want 401)"

# An INVALID session id → 401 (credential present but bad; no fall-through to
# anonymous/weaker auth).
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -XPOST http://127.0.0.1:$WEB/_rpc \
            -H 'picomesh-sid: deadbeefdeadbeefdeadbeefdeadbeef' \
            -H 'Content-Type: application/json' \
            -d '{"path":"git_repo.git_repo.count_total","args":[]}')
[ "$code" = "401" ] && note_pass "invalid sid → 401 (no credential downgrade)" \
                     || note_fail "invalid sid returned $code (want 401)"

# An invalid/malformed BEARER JWT → 401 (the bearer_jwt_token authenticator
# matched the credential shape but verification failed; no fall-through).
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -XPOST http://127.0.0.1:$WEB/_rpc \
            -H 'Authorization: Bearer aaaa.bbbb.cccc' \
            -H 'Content-Type: application/json' \
            -d '{"path":"git_repo.git_repo.count_total","args":[]}')
[ "$code" = "401" ] && note_pass "malformed bearer JWT → 401 (issue #27)" \
                     || note_fail "malformed bearer JWT returned $code (want 401)"

# A NON-Bearer Authorization scheme (Basic) must NOT be treated as a bearer
# token: it is no-match → request is anonymous → an authed endpoint answers 401
# with "authentication required" (NOT a bearer "verification failed"). (issue #27)
resp=$(curl -sS --max-time 10 -XPOST http://127.0.0.1:$WEB/_rpc \
            -H 'Authorization: Basic dXNlcjpwYXNzd29yZA==' \
            -H 'Content-Type: application/json' \
            -d '{"path":"git_repo.git_repo.count_total","args":[]}')
if [[ "$resp" == *"authentication required"* ]]; then
    note_pass "non-Bearer Authorization is no-match (anonymous, not a failed bearer)"
else
    note_fail "non-Bearer Authorization mishandled — got: ${resp:0:160}"
fi

# A method ABSENT from the policy is denied by default → 403, even for a
# signed-in user (accounts.exists is not in the gateway policy).
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -b $COOKIES -XPOST http://127.0.0.1:$WEB/_rpc \
            -H 'Content-Type: application/json' \
            -d '{"path":"accounts.accounts.exists","args":[1]}')
[ "$code" = "403" ] && note_pass "policy default-deny: unlisted method → 403" \
                     || note_fail "unlisted method returned $code (want 403)"

# Credential-exchange methods are never callable via public /_rpc → 403.
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -b $COOKIES -XPOST http://127.0.0.1:$WEB/_rpc \
            -H 'Content-Type: application/json' \
            -d "{\"path\":\"session.session.lookup\",\"args\":[\"${SID_VAL:-x}\"]}")
[ "$code" = "403" ] && note_pass "credential-exchange session.lookup blocked → 403" \
                     || note_fail "session.lookup via /_rpc returned $code (want 403)"

# A regular user (alice) is NOT site-owner → an owner/site-gated admin method
# returns 403 (role insufficient), proving role-gating through the policy.
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -b $COOKIES -XPOST http://127.0.0.1:$WEB/_rpc \
            -H 'Content-Type: application/json' \
            -d '{"path":"accounts.accounts.count","args":[]}')
[ "$code" = "403" ] && note_pass "role-gate: non-owner alice → accounts.count 403" \
                     || note_fail "non-owner accounts.count returned $code (want 403)"

# /_whoami exposes only non-sensitive claims — never the JWT or a secret.
who=$(curl -sS --max-time 10 -b $COOKIES http://127.0.0.1:$WEB/_whoami)
expect_contains '/_whoami returns alice uid' "$who" '"username":"alice"'
if [[ "$who" != *jwt* && "$who" != *access_jwt* ]]; then
    note_pass "/_whoami leaks no JWT"
else
    note_fail "/_whoami exposed a JWT: ${who:0:120}"
fi

# Parent control still alive.
out=$(http_post "$CTRL" /invoke "{\"method\":\"mesh_mesh_count_children\",\"handle\":$H,\"args\":[]}")
expect_contains "parent.count_children == $SPAWNED" "$out" "\"result\":$SPAWNED"

echo
# The webapp uses SO_REUSEPORT (shared loop listener), so if a stale
# picoforge-webapp from a prior run already holds :$SIDE, a fresh one would
# silently CO-BIND it — the kernel load-balances requests across both and half
# hit the stale binary (e.g. pre-rename RPC paths → "service unreachable").
# We can't reach around to kill that leaked process here, so instead we refuse
# to share the port: if :$SIDE is already held, bring the webapp up on a free
# port and tell the operator exactly which pid to stop to reclaim :$SIDE.
if ss -ltn 2>/dev/null | grep -q ":${SIDE} "; then
    stale=$(ss -ltnp 2>/dev/null | grep ":${SIDE} " | grep -oE 'pid=[0-9]+' \
              | cut -d= -f2 | sort -un | paste -sd' ' -)
    altside=""
    for cand in $(seq $((SIDE+1)) $((SIDE+20))); do
        ss -ltn 2>/dev/null | grep -q ":${cand} " || { altside=$cand; break; }
    done
    [ -z "$altside" ] && altside=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')
    echo "  ⚠ :${SIDE} is held by a stale process (pid ${stale:-unknown}) — a leaked"
    echo "    picoforge-webapp from an earlier run. Bringing the webapp up on :${altside}"
    echo "    instead so the mesh comes up clean. To reclaim :${SIDE}: 'kill ${stale:-<pid>}'"
    echo "    (it is a plain webapp — SIGTERM stops it) and re-run."
    SIDE=$altside
fi

echo "[5b] standalone picoforge-webapp → gateway /_rpc on :${SIDE}"
# The separated browser webapp: links no plugins, knows no backend
# ports — every call leaves it as POST /_rpc against the gateway. This
# proves the chosen architecture (browser → picoforge-webapp → gateway →
# backends), distinct from the gateway serving its own HTML above.
"$WEBAPP" --gateway-url "http://127.0.0.1:${WEB}" \
    --host 127.0.0.1 --port "$SIDE" \
    --static assets/picoforge/static \
    --github-client "${PICOFORGE_GITHUB_CLIENT:-}" \
    --public-url "${PICOFORGE_PUBLIC_URL:-http://127.0.0.1:${SIDE}}" \
    > $SIDECAR_LOG 2>&1 &
SIDECAR=$!
sleep 0.7

# Sanity: our sidecar must now be the SOLE listener on :$SIDE (we picked a free
# port above, so this should always hold — it guards against a TOCTOU race).
holders=$(ss -ltnp 2>/dev/null | grep ":${SIDE} " | grep -oE 'pid=[0-9]+' \
            | cut -d= -f2 | sort -un | paste -sd' ' -)
if [ "$holders" != "$SIDECAR" ]; then
    note_fail "picoforge-webapp is not the sole listener on :${SIDE} (holders: [${holders:-none}], ours: ${SIDECAR})."
    tail -3 $SIDECAR_LOG >&2
fi

# GET sidecar /login renders its own sign-in form (not the gateway's).
out=$(curl -sS --max-time 10 "http://127.0.0.1:${SIDE}/-/login")
expect_contains 'sidecar GET /login renders' "$out" '<h1>Sign in</h1>'

# The /login page links to /register, so the webapp MUST serve it (this
# was a 404 hole). GET renders the form; POST relays to the gateway and,
# for a brand-new account, returns 303 + Set-Cookie like /login does.
out=$(curl -sS --max-time 10 "http://127.0.0.1:${SIDE}/-/register")
expect_contains 'webapp GET /register renders' "$out" '<h1>Create account</h1>'
hdrs=$(curl -sS --max-time 10 -D - -o /dev/null \
            -XPOST "http://127.0.0.1:${SIDE}/-/register" \
            --data-urlencode 'username=carol' --data-urlencode 'password=hunter2')
expect_contains 'webapp POST /register (new user) → 303 /repos' "$hdrs" '303 See Other.*[Ll]ocation: /-/repos|[Ll]ocation: /-/-/repos.*303'
expect_contains 'webapp POST /register relays sid cookie' "$hdrs" 'Set-Cookie: picomesh-sid='

# POST sidecar /login forwards the form to the gateway's composite
# login; the gateway authenticates alice (registered above), mints a
# session and answers 303 + Set-Cookie, which the sidecar relays.
rm -f $SIDE_COOKIES
hdrs=$(curl -sS --max-time 10 -D - -o /dev/null -c $SIDE_COOKIES \
            -XPOST "http://127.0.0.1:${SIDE}/-/login" \
            --data-urlencode 'username=alice' --data-urlencode 'password=hunter2')
expect_contains 'sidecar POST /login → 303 /repos' "$hdrs" '303 See Other.*[Ll]ocation: /-/repos|[Ll]ocation: /-/-/repos.*303'
expect_contains 'sidecar relays gateway sid cookie' "$hdrs" 'Set-Cookie: picomesh-sid='

# GET sidecar /repos renders a data page sourced from the gateway via
# /_rpc (git_repo.git_repo.count_total) — the sidecar holds no plugins.
out=$(curl -sS --max-time 10 -b $SIDE_COOKIES "http://127.0.0.1:${SIDE}/-/repos")
expect_contains 'sidecar GET /repos renders'           "$out" '<h1>Repositories</h1>'
expect_contains 'sidecar /repos sourced via gateway /_rpc' "$out" 'via the gateway'

# The full page set the gateway no longer serves now renders on the webapp,
# each driven by a live service discovered from the gateway's /_describe and
# sourced via /_rpc. These are the M4 pages: account landing, issues,
# pipeline runs, admin users.
out=$(curl -sS --max-time 10 -b $SIDE_COOKIES "http://127.0.0.1:${SIDE}/alice")
expect_contains 'webapp GET /<account> (account landing)' "$out" '<h1>alice</h1>'
# Repo-scoped pages carry the repo name as <h1> (project header) and the
# section name in the panel header + the active project tab.
out=$(curl -sS --max-time 10 -b $SIDE_COOKIES "http://127.0.0.1:${SIDE}/alice/website/-/issues")
expect_contains 'webapp GET /<acct>/<repo>/issues'        "$out" '<strong>Issues</strong>'
expect_contains 'webapp issues page active tab'           "$out" 'class="active" href="/alice/website/-/issues"'
out=$(curl -sS --max-time 10 -b $SIDE_COOKIES "http://127.0.0.1:${SIDE}/alice/website/-/runs")
expect_contains 'webapp GET /<acct>/<repo>/runs'          "$out" '<strong>Pipeline runs</strong>'
# Admin space is gated on a signed-in SITE ADMIN, enforced BEFORE any
# admin content renders. alice is a regular user → 403; an anonymous
# caller → 303 to /login. Only root (the site-owner bootstrapped at the
# first /register) may see the admin pages.
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' \
            "http://127.0.0.1:${SIDE}/-/admin")
[ "$code" = "303" ] && note_pass "anonymous /admin → 303 (redirect to login)" \
                    || note_fail "anonymous /admin returned $code (want 303)"
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' \
            -b $SIDE_COOKIES "http://127.0.0.1:${SIDE}/-/admin")
[ "$code" = "403" ] && note_pass "non-admin alice /admin → 403 (forbidden before render)" \
                    || note_fail "non-admin alice /admin returned $code (want 403)"

# Sign in as root (the site owner) to view the admin area.
rm -f $ROOT_COOKIES
curl -sS --max-time 10 -c $ROOT_COOKIES -o /dev/null -XPOST \
     "http://127.0.0.1:${SIDE}/-/login" \
     --data-urlencode 'username=root' --data-urlencode 'password=rootpw'

# The Admin area is its OWN section with its OWN left menu (distinct from
# the project nav). /admin is the overview; each aspect is a page; every
# admin page shows the "Admin area" sidebar, not the project sidebar.
out=$(curl -sS --max-time 10 -b $ROOT_COOKIES "http://127.0.0.1:${SIDE}/-/admin")
expect_contains 'webapp GET /admin (overview, as admin)'  "$out" '<h1>Admin</h1>'
expect_contains 'admin area has its own sidebar'          "$out" 'Admin area'
expect_contains 'admin overview links to Repositories'    "$out" 'href="/-/admin/repos"'
out=$(curl -sS --max-time 10 -b $ROOT_COOKIES "http://127.0.0.1:${SIDE}/-/admin/users")
expect_contains 'webapp GET /admin/users'                 "$out" '<h1>Users</h1>'
expect_contains 'admin/users uses admin sidebar'          "$out" 'class="active" href="/-/admin/users"'
expect_contains 'admin/users lists real registered users' "$out" 'alice'
expect_contains 'admin sidebar excludes Projects nav'     "$out" 'href="/-/admin/services"'
out=$(curl -sS --max-time 10 -b $ROOT_COOKIES "http://127.0.0.1:${SIDE}/-/admin/repos")
expect_contains 'webapp GET /admin/repos'                 "$out" '<h1>Repositories</h1>'
out=$(curl -sS --max-time 10 -b $ROOT_COOKIES "http://127.0.0.1:${SIDE}/-/admin/tokens")
expect_contains 'webapp GET /admin/tokens'                "$out" '<h1>Tokens</h1>'
# The admin RBAC (Namespaces) area is exercised after [5e] provisions a group.

# New GitLab-like shell additions (gh#10): repo settings tab, services
# health page, cross-repo dashboards, and the dedicated new-repo form —
# all sourced from the gateway, all inside the same shell.
out=$(curl -sS --max-time 10 -b $SIDE_COOKIES "http://127.0.0.1:${SIDE}/alice/website/-/settings")
expect_contains 'webapp GET /<acct>/<repo>/settings'      "$out" 'class="active" href="/alice/website/-/settings"'
out=$(curl -sS --max-time 10 -b $ROOT_COOKIES "http://127.0.0.1:${SIDE}/-/admin/services")
expect_contains 'webapp GET /admin/services lists services' "$out" 'service-table'
expect_contains 'webapp /admin/services shows git_repo'   "$out" 'git_repo'
out=$(curl -sS --max-time 10 -b $SIDE_COOKIES "http://127.0.0.1:${SIDE}/-/dashboard/issues")
expect_contains 'webapp GET /dashboard/issues'            "$out" 'Open issues across your repositories'
out=$(curl -sS --max-time 10 -b $SIDE_COOKIES "http://127.0.0.1:${SIDE}/-/dashboard/runs")
expect_contains 'webapp GET /dashboard/runs'              "$out" 'pipeline-table'
out=$(curl -sS --max-time 10 -b $SIDE_COOKIES "http://127.0.0.1:${SIDE}/-/repos/new")
expect_contains 'webapp GET /repos/new (form)'            "$out" '<h1>New repository</h1>'
# Every signed-in page wears the same shell (topbar + sidebar).
expect_contains 'webapp pages share the shell'            "$out" 'class="sidebar-nav"'

# Same pages refuse on the GATEWAY — it serves no HTML (B1 / gh#5 invariant).
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' "http://127.0.0.1:${WEB}/admin/users")
[ "$code" = "404" ] && note_pass "gateway refuses GET /admin/users (404 — page is the webapp's)" \
                     || note_fail "gateway GET /admin/users returned $code (want 404)"

# Direct gateway-API checks: yaapp-style surface present, legacy gone.
# The gateway gates /_rpc (issue #19), so this read carries alice's session
# cookie (captured by the sidecar login above).
out=$(curl -sS --max-time 10 -b $SIDE_COOKIES -XPOST "http://127.0.0.1:${WEB}/_rpc" \
           -H 'Content-Type: application/json' \
           -d '{"path":"git_repo.git_repo.count_total","args":[]}')
expect_contains 'gateway POST /_rpc {path,args} (authed)' "$out" '"result":'

# Authenticated /_rpc round-trip returning a meaningful non-zero result —
# alice created /alice/website above, so count_total is >= 1. Proves the
# authenticated forward + remote round-trip end to end. (session.lookup is no
# longer a public method — it is an authenticator-internal credential
# exchange, exercised via the 403 check in [5a].)
out=$(curl -sS --max-time 10 -b $SIDE_COOKIES -XPOST "http://127.0.0.1:${WEB}/_rpc" \
           -H 'Content-Type: application/json' \
           -d '{"path":"git_repo.git_repo.count_total","args":[]}')
expect_contains 'gateway /_rpc authed round-trip (non-zero result)' "$out" '"result":[1-9][0-9]*'

# Malformed path → stable 400; unknown method → 404.
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -XPOST "http://127.0.0.1:${WEB}/_rpc" \
            -H 'Content-Type: application/json' -d '{"path":"oneword","args":[]}')
[ "$code" = "400" ] && note_pass "gateway /_rpc malformed path → 400" \
                     || note_fail "gateway /_rpc malformed path returned $code (want 400)"
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -XPOST "http://127.0.0.1:${WEB}/_rpc" \
            -H 'Content-Type: application/json' -d '{"path":"git_repo.git_repo.no_such_method","args":[]}')
[ "$code" = "404" ] && note_pass "gateway /_rpc unknown method → 404" \
                     || note_fail "gateway /_rpc unknown method returned $code (want 404)"

# _describe contract: root lists services, class form lists methods.
out=$(curl -sS --max-time 10 "http://127.0.0.1:${WEB}/_describe")
expect_contains 'gateway GET /_describe lists services' "$out" '"services":\['
out=$(curl -sS --max-time 10 "http://127.0.0.1:${WEB}/git_repo.git_repo/_describe")
expect_contains 'gateway /<class>/_describe lists methods' "$out" 'git_repo_git_repo_count_total'

# gh#15: generic service-console tooling. Two SEPARATE, app-agnostic nodes
# (no picoforge routes): a yrpc->yhttp transport bridge (NOT the gateway — it
# does not front `session`, so it is not the auth boundary) and the standalone
# /_alpine console frontend that proxies to it. Both used to be hardcoded to
# :8230/:8231 — now they get per-instance ports from this run's window (set on
# the parent via --config above) so two meshes don't collide on them.
# The bridge fronts its configured remotes over the generic JSON API; the
# console builds its UI from /_describe and invokes through JSON /_rpc.
# See docs/service-console.md.

out=$(curl -sS --max-time 10 "http://127.0.0.1:${BRIDGE}/_describe")
expect_contains 'bridge GET /_describe lists active services' "$out" '"services":\['
expect_contains 'bridge /_describe enriches services with classes/methods' "$out" 'git_repo_git_repo_count_total'
out=$(curl -sS --max-time 10 -XPOST "http://127.0.0.1:${BRIDGE}/_rpc" \
           -H 'Content-Type: application/json' -d '{"path":"git_repo.git_repo.count_total","args":[]}')
expect_contains 'bridge POST /_rpc forwards to the yrpc backend' "$out" '"result":'
# The bridge is pure transport: NO console, NO HTML/auth surface.
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' "http://127.0.0.1:${BRIDGE}/_alpine")
[ "$code" = "404" ] && note_pass "bridge does not serve /_alpine (separation invariant)" \
                     || note_fail "bridge GET /_alpine returned $code (want 404)"
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' "http://127.0.0.1:${BRIDGE}/login")
[ "$code" = "404" ] && note_pass "bridge serves no auth/HTML route (404 /login)" \
                     || note_fail "bridge GET /login returned $code (want 404)"

out=$(curl -sS --max-time 10 "http://127.0.0.1:${CONSOLE}/_alpine")
expect_contains 'console GET /_alpine serves the generic page' "$out" 'picomesh service console'
out=$(curl -sS --max-time 10 -XPOST "http://127.0.0.1:${CONSOLE}/_describe" -d '{}')
expect_contains 'console proxies /_describe to the upstream bridge' "$out" '"services":\['
out=$(curl -sS --max-time 10 -XPOST "http://127.0.0.1:${CONSOLE}/_rpc" \
           -H 'Content-Type: application/json' -d '{"path":"git_repo.git_repo.count_total","args":[]}')
expect_contains 'console proxies POST /_rpc to the upstream bridge' "$out" '"result":'

# picotrace is a mesh-managed loopback webapp under mesh.webapps. It talks
# directly to trace_collector; it is not a Picoforge route and not exposed by
# the public gateway.
echo "[5b/6] picotrace is mesh-managed under mesh.webapps"

# Legacy public surface retired on the gateway (8090)…
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' \
            -XPOST "http://127.0.0.1:${WEB}/create" -d '{"class":"git_repo_git_repo"}')
[ "$code" = "410" ] && note_pass "gateway retired POST /create (410)" \
                     || note_fail "gateway POST /create returned $code (want 410)"
# …but the mesh control parent (8800) MUST still serve it for bootstrap.
out=$(http_post "$CTRL" /create '{"class":"mesh_mesh"}')
expect_contains 'control parent :8800 still serves /create' "$out" '"handle":[0-9]+'

# Keep the webapp sidecar ALIVE — it is the live page tier the banner points
# the operator at (http://…:${SIDE}/-/login). The cleanup trap SIGTERMs it on
# exit. (It used to be SIGKILLed here, which left the "live mesh" with no
# browser UI and forced the operator to hand-start picoforge-webapp — the very
# thing that leaks orphan webapps onto :8080.)

echo
echo "[5c] distributed tracing — the trace collector reconstructs a request tree"
# Issue #11: a request entering the gateway accepts/continues a W3C trace
# context; each hop records a span with trace_id/span_id/parent_span_id and
# ships it (fire-and-forget) to the trace_collector backend plugin. The
# collector is reached for queries like any backend — through the gateway's
# /_rpc (service-driven), NOT a bespoke port.
TRACE_ID=0123456789abcdef0123456789abcdef
PARENT_SPAN=1122334455667788

# Issue a traced request through the gateway. It continues our inbound
# traceparent, opens the root span, and forwards to git_repo (which reads
# sharded_storage) — so one trace_id fans out into a gateway → git_repo →
# sharded_storage span chain.
traced=$(curl -sS --max-time 10 -D - -o /dev/null -b $SIDE_COOKIES -XPOST "http://127.0.0.1:${WEB}/_rpc" \
    -H "traceparent: 00-${TRACE_ID}-${PARENT_SPAN}-01" \
    -H 'Content-Type: application/json' \
    -d '{"path":"git_repo.git_repo.count_total","args":[]}')
expect_contains 'gateway echoes a traceparent for our trace' "$traced" "traceparent: 00-${TRACE_ID}-"

# Spans ship fire-and-forget — give the collector a moment to ingest.
sleep 1.5

# Query the trace back through the gateway: trace_collector.trace_collector.get_trace
# returns the whole trace as a JSON string in the /_rpc "result" field.
trace_rpc=$(curl -sS --max-time 10 -b $SIDE_COOKIES -XPOST "http://127.0.0.1:${WEB}/_rpc" \
    -H 'Content-Type: application/json' \
    -d "{\"path\":\"trace_collector.trace_collector.get_trace\",\"args\":[\"${TRACE_ID}\"]}")
# Reconstruct the tree: >= 2 spans across >1 service, with a child whose
# parent_span_id resolves to another span (a real parent/child tree).
tree_ok=$(printf '%s' "$trace_rpc" | python3 -c '
import sys, json
outer = json.load(sys.stdin)
d = json.loads(outer["result"])           # result is a JSON string
spans = d.get("spans", [])
ids = {s["span_id"] for s in spans}
linked = any(s.get("parent_span_id") and s["parent_span_id"] in ids for s in spans)
svcs = {s["service_name"] for s in spans}
names = " ".join(s.get("name","") for s in spans)
ok = len(spans) >= 2 and linked and len(svcs) >= 2 and "gateway." in names
print("OK" if ok else "NO len=%d linked=%s svcs=%s" % (len(spans), linked, sorted(svcs)))
' 2>/dev/null)
expect_contains 'collector reconstructs a linked cross-service span tree' "$tree_ok" 'OK'

# Service + latency aggregates are queryable through the gateway too.
svc=$(curl -sS --max-time 10 -b $SIDE_COOKIES -XPOST "http://127.0.0.1:${WEB}/_rpc" \
    -H 'Content-Type: application/json' \
    -d '{"path":"trace_collector.trace_collector.services","args":[]}')
expect_contains 'collector services() lists the gateway'  "$svc" 'gateway'
expect_contains 'collector services() lists git_repo'     "$svc" 'git_repo'
lat=$(curl -sS --max-time 10 -b $SIDE_COOKIES -XPOST "http://127.0.0.1:${WEB}/_rpc" \
    -H 'Content-Type: application/json' \
    -d '{"path":"trace_collector.trace_collector.latency","args":["gateway","",0]}')
expect_contains 'collector latency() returns an aggregate' "$lat" 'p50_ns'

# The local op aggregate moved to /_perf; /_trace is demoted to a pointer.
perf=$(curl -sS --max-time 10 "http://127.0.0.1:${WEB}/_perf")
expect_contains 'gateway /_perf shows the local latency aggregate' "$perf" 'p50_us'
moved=$(curl -sS --max-time 10 "http://127.0.0.1:${WEB}/_trace")
expect_contains 'gateway /_trace is demoted, points at /_perf' "$moved" '/_perf'

echo
echo "[5d] runner agent (docs/runner-agent.md) — token → register → lease → log → complete"
# External CI runner agents authenticate to the gateway with an opaque rnr_
# bearer token, register, then poll-lease pipeline jobs from git_pipeline. The
# gateway resolves the rnr_ token to a runner JWT (groups site:runner,runner:<id>)
# so the policy can gate the runner-only lifecycle. root (site-owner) mints the
# tokens; alice (regular user) triggers builds; the runner executes them.
JSONH='Content-Type: application/json'

# enqueue_job is now namespace-role-gated (developer+ on the repo's namespace,
# issue #30), so jobs must target a REAL repo the caller can build. alice owns
# /alice/website (created above); its deterministic repo_id is the FNV-1a of
# "alice/website" (same hash the gateway/git_repo use).
PIPE_REPO=$(python3 -c 'h=2166136261
for c in b"alice/website": h=((h^c)*16777619)&0xffffffff
print(h or 1)')

# Admin (site-owner root) mints a runner token. create_token is owner-gated.
ct=$(curl -sS --max-time 10 -b $ROOT_COOKIES -XPOST "http://127.0.0.1:${WEB}/_rpc" \
     -H "$JSONH" -d '{"path":"runner_agent.runner_agent.create_token","args":["ci-runner-1","linux,x86_64"]}')
expect_contains 'admin create_token mints an rnr_ token' "$ct" '"token":"rnr_'
RUNNER_TOKEN=$(printf '%s' "$ct" | python3 -c 'import sys,json;print(json.load(sys.stdin)["result"]["token"])' 2>/dev/null)
RUNNER_ID=$(printf '%s' "$ct" | python3 -c 'import sys,json;print(json.load(sys.stdin)["result"]["runner_id"])' 2>/dev/null)

# A non-owner (alice) must NOT be able to mint a runner token → 403.
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -b $SIDE_COOKIES -XPOST "http://127.0.0.1:${WEB}/_rpc" \
            -H "$JSONH" -d '{"path":"runner_agent.runner_agent.create_token","args":["evil","linux"]}')
[ "$code" = "403" ] && note_pass "non-owner alice create_token → 403" \
                    || note_fail "alice create_token returned $code (want 403)"

# The runner registers itself with its opaque token (Authorization: Bearer rnr_).
reg=$(curl -sS --max-time 10 -XPOST "http://127.0.0.1:${WEB}/_rpc" \
      -H "Authorization: Bearer ${RUNNER_TOKEN}" -H "$JSONH" \
      -d "{\"path\":\"runner_agent.runner_agent.register\",\"args\":[${RUNNER_ID},\"ci-runner-1\",\"linux,x86_64\",\"0.1.0\",\"smoke-host\"]}")
expect_contains 'runner register echoes its runner_id' "$reg" "\"result\":${RUNNER_ID}"

# A user cookie carries no site:runner group → register is forbidden (403).
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -b $SIDE_COOKIES -XPOST "http://127.0.0.1:${WEB}/_rpc" \
            -H "$JSONH" -d "{\"path\":\"runner_agent.runner_agent.register\",\"args\":[${RUNNER_ID},\"x\",\"linux\",\"0\",\"h\"]}")
[ "$code" = "403" ] && note_pass "non-runner alice register → 403" \
                    || note_fail "alice register returned $code (want 403)"

# A user (alice) enqueues a CI job with execution metadata; the runner leases it.
ej=$(curl -sS --max-time 10 -b $SIDE_COOKIES -XPOST "http://127.0.0.1:${WEB}/_rpc" \
     -H "$JSONH" -d '{"path":"git_pipeline.git_pipeline.enqueue_job","args":['"$PIPE_REPO"',"refs/heads/main","",60]}')
expect_contains 'user enqueue_job returns a job id' "$ej" '"result":[1-9][0-9]*'
JOB_ID=$(printf '%s' "$ej" | python3 -c 'import sys,json;print(json.load(sys.stdin)["result"])' 2>/dev/null)

# A non-runner (alice) cannot lease a job → 403 (lease_job is runner-gated).
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -b $SIDE_COOKIES -XPOST "http://127.0.0.1:${WEB}/_rpc" \
            -H "$JSONH" -d "{\"path\":\"git_pipeline.git_pipeline.lease_job\",\"args\":[${RUNNER_ID},\"linux\"]}")
[ "$code" = "403" ] && note_pass "non-runner alice lease_job → 403" \
                    || note_fail "alice lease_job returned $code (want 403)"

# The runner leases the next queued job and receives the full descriptor.
lj=$(curl -sS --max-time 10 -XPOST "http://127.0.0.1:${WEB}/_rpc" \
     -H "Authorization: Bearer ${RUNNER_TOKEN}" -H "$JSONH" \
     -d "{\"path\":\"git_pipeline.git_pipeline.lease_job\",\"args\":[${RUNNER_ID},\"linux,x86_64\"]}")
expect_contains 'runner lease_job returns the queued job' "$lj" "\"job_id\":${JOB_ID}"
expect_contains 'lease_job descriptor carries the ref + expiry' "$lj" 'refs/heads/main'

# The runner streams a log chunk (owner-gated), then reads it back.
al=$(curl -sS --max-time 10 -XPOST "http://127.0.0.1:${WEB}/_rpc" \
     -H "Authorization: Bearer ${RUNNER_TOKEN}" -H "$JSONH" \
     -d "{\"path\":\"git_pipeline.git_pipeline.append_log\",\"args\":[${JOB_ID},0,\"hello from runner\n\"]}")
expect_contains 'runner append_log returns the new length' "$al" '"result":[1-9][0-9]*'
rl=$(curl -sS --max-time 10 -XPOST "http://127.0.0.1:${WEB}/_rpc" \
     -H "Authorization: Bearer ${RUNNER_TOKEN}" -H "$JSONH" \
     -d "{\"path\":\"git_pipeline.git_pipeline.read_log\",\"args\":[${JOB_ID}]}")
expect_contains 'read_log echoes the streamed chunk' "$rl" 'hello from runner'

# The runner reports success → count_done bumps.
cj=$(curl -sS --max-time 10 -XPOST "http://127.0.0.1:${WEB}/_rpc" \
     -H "Authorization: Bearer ${RUNNER_TOKEN}" -H "$JSONH" \
     -d "{\"path\":\"git_pipeline.git_pipeline.complete_job\",\"args\":[${JOB_ID},0,\"build OK\"]}")
expect_contains 'runner complete_job → 1' "$cj" '"result":1'

# Ownership: a SECOND runner cannot complete the FIRST runner's job.
ct2=$(curl -sS --max-time 10 -b $ROOT_COOKIES -XPOST "http://127.0.0.1:${WEB}/_rpc" \
      -H "$JSONH" -d '{"path":"runner_agent.runner_agent.create_token","args":["ci-runner-2","linux"]}')
RUNNER_TOKEN2=$(printf '%s' "$ct2" | python3 -c 'import sys,json;print(json.load(sys.stdin)["result"]["token"])' 2>/dev/null)
RUNNER_ID2=$(printf '%s' "$ct2" | python3 -c 'import sys,json;print(json.load(sys.stdin)["result"]["runner_id"])' 2>/dev/null)
curl -sS --max-time 10 -o /dev/null -XPOST "http://127.0.0.1:${WEB}/_rpc" \
     -H "Authorization: Bearer ${RUNNER_TOKEN2}" -H "$JSONH" \
     -d "{\"path\":\"runner_agent.runner_agent.register\",\"args\":[${RUNNER_ID2},\"ci-runner-2\",\"linux\",\"0.1.0\",\"smoke-host\"]}"
ej2=$(curl -sS --max-time 10 -b $SIDE_COOKIES -XPOST "http://127.0.0.1:${WEB}/_rpc" \
      -H "$JSONH" -d '{"path":"git_pipeline.git_pipeline.enqueue_job","args":['"$PIPE_REPO"',"refs/heads/dev","",60]}')
JOB_ID2=$(printf '%s' "$ej2" | python3 -c 'import sys,json;print(json.load(sys.stdin)["result"])' 2>/dev/null)
curl -sS --max-time 10 -o /dev/null -XPOST "http://127.0.0.1:${WEB}/_rpc" \
     -H "Authorization: Bearer ${RUNNER_TOKEN2}" -H "$JSONH" \
     -d "{\"path\":\"git_pipeline.git_pipeline.lease_job\",\"args\":[${RUNNER_ID2},\"linux\"]}"
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -XPOST "http://127.0.0.1:${WEB}/_rpc" \
            -H "Authorization: Bearer ${RUNNER_TOKEN}" -H "$JSONH" \
            -d "{\"path\":\"git_pipeline.git_pipeline.complete_job\",\"args\":[${JOB_ID2},0,\"steal\"]}")
[ "$code" = "500" ] && note_pass "ownership: runner1 cannot complete runner2's job (500)" \
                    || note_fail "cross-runner complete_job returned $code (want 500)"
# runner2 completes its own job cleanly.
cj2=$(curl -sS --max-time 10 -XPOST "http://127.0.0.1:${WEB}/_rpc" \
      -H "Authorization: Bearer ${RUNNER_TOKEN2}" -H "$JSONH" \
      -d "{\"path\":\"git_pipeline.git_pipeline.complete_job\",\"args\":[${JOB_ID2},0,\"ok\"]}")
expect_contains 'runner2 complete_job (own job) → 1' "$cj2" '"result":1'

# Long-poll lease (GitLab/GitHub-runner style): with the queue empty, a
# lease_job that asks to wait holds open server-side; a job enqueued mid-wait is
# delivered on the SAME held call, not a later poll. Enqueue ~1s into a 4s wait.
( sleep 1
  curl -sS --max-time 10 -o /dev/null -b $SIDE_COOKIES -XPOST "http://127.0.0.1:${WEB}/_rpc" \
       -H "$JSONH" -d '{"path":"git_pipeline.git_pipeline.enqueue_job","args":['"$PIPE_REPO"',"refs/heads/longpoll","",60]}' ) &
lp_enqueue_pid=$!
lp=$(curl -sS --max-time 10 -XPOST "http://127.0.0.1:${WEB}/_rpc" \
     -H "Authorization: Bearer ${RUNNER_TOKEN}" -H "$JSONH" \
     -d "{\"path\":\"git_pipeline.git_pipeline.lease_job\",\"args\":[${RUNNER_ID},\"linux\",4000]}")
wait "$lp_enqueue_pid" 2>/dev/null || true
expect_contains 'long-poll lease_job delivers a job enqueued mid-wait' "$lp" 'refs/heads/longpoll'
LP_JOB=$(printf '%s' "$lp" | python3 -c 'import sys,json;print(json.load(sys.stdin)["result"]["job_id"])' 2>/dev/null)
curl -sS --max-time 10 -o /dev/null -XPOST "http://127.0.0.1:${WEB}/_rpc" \
     -H "Authorization: Bearer ${RUNNER_TOKEN}" -H "$JSONH" \
     -d "{\"path\":\"git_pipeline.git_pipeline.complete_job\",\"args\":[${LP_JOB},0,\"longpoll ok\"]}"

# Drive the REAL runner-agent.py once, end to end: a fresh job is queued, the
# agent leases it, runs the stub build, streams logs, and completes it.
curl -sS --max-time 10 -o /dev/null -b $SIDE_COOKIES -XPOST "http://127.0.0.1:${WEB}/_rpc" \
     -H "$JSONH" -d '{"path":"git_pipeline.git_pipeline.enqueue_job","args":['"$PIPE_REPO"',"refs/heads/agent","",60]}'
PICOFORGE_RUNNER_TOKEN="$RUNNER_TOKEN" python3 tools/picoforge/runner-agent/runner-agent.py \
    --gateway "http://127.0.0.1:${WEB}" --runner-id "$RUNNER_ID" --once \
    > $RUNNER_LOG 2>&1
expect_contains 'runner-agent.py drives a job to completion' "$(cat $RUNNER_LOG)" 'completed job'

echo
echo "[5e] namespace RBAC (issue #30) — group roles, inheritance, site bypass"
# Everything goes through the gateway /_rpc with the actor's session cookie,
# exactly like a real client. Roles are minted into the JWT at login from the
# canonical namespace_members table (point-in-time claims).
gw_rpc()   { curl -sS --max-time 10 -b "$1" -XPOST "http://127.0.0.1:${WEB}/_rpc" -H 'Content-Type: application/json' -d "$2"; }
gw_code()  { curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -b "$1" -XPOST "http://127.0.0.1:${WEB}/_rpc" -H 'Content-Type: application/json' -d "$2"; }
rb_uid()   { curl -sS --max-time 10 -b "$1" "http://127.0.0.1:${WEB}/_whoami" | python3 -c 'import sys,json;print(json.load(sys.stdin)["uid"])' 2>/dev/null; }
rb_result(){ python3 -c 'import sys,json;print(json.load(sys.stdin).get("result",""))' 2>/dev/null; }

# RBAC actors: bob (→developer), erin (→reporter), dave (no role).
for u in bob erin dave; do
    curl -sS --max-time 10 -o /dev/null -c "$LOG_DIR/rb-$u.txt" -XPOST "http://127.0.0.1:${WEB}/register" \
         --data-urlencode "username=$u" --data-urlencode "password=pw-$u"
done
# NB: EUID/UID/GID are reserved readonly shell variables — never assign them.
RUID=$(rb_uid $ROOT_COOKIES); AUID=$(rb_uid $SIDE_COOKIES)
BOB_UID=$(rb_uid $LOG_DIR/rb-bob.txt); ERIN_UID=$(rb_uid $LOG_DIR/rb-erin.txt); DAVE_UID=$(rb_uid $LOG_DIR/rb-dave.txt)

# (1) Personal-namespace owner manages her own repo: alice creates a repo in HER
# namespace (developer+ on namespace_path "alice" — she owns it) and pushes a
# file. Exercises role_scope namespace_path (make) + repo_namespace (put_file).
amk=$(gw_rpc $SIDE_COOKIES '{"path":"git_repo.git_repo.make","args":['"$AUID"',"alice","rbacrepo"]}')
# (uses AUID; bob/erin/dave uids: BOB_UID/ERIN_UID/DAVE_UID)
ARID=$(printf '%s' "$amk" | rb_result)
expect_contains 'rbac: personal-ns owner creates own repo' "$amk" '"result":[1-9]'
apush=$(gw_rpc $SIDE_COOKIES '{"path":"git_repo.git_repo.put_file","args":['"$ARID"',"README.md","hi","init","alice","a@x"]}')
expect_contains 'rbac: personal-ns owner pushes own repo' "$apush" '"result":"[0-9a-f]'

# Root (site owner) builds a group with members, a subgroup, and two repos.
gw_rpc $ROOT_COOKIES '{"path":"accounts.accounts.ns_create","args":['"$RUID"',"group","acme",""]}' >/dev/null
mem=$(gw_rpc $ROOT_COOKIES '{"path":"accounts.accounts.ns_add_member","args":["acme",'"$BOB_UID"',"developer"]}')
expect_contains 'rbac: site-admin grants a group membership' "$mem" '"result":1'
gw_rpc $ROOT_COOKIES '{"path":"accounts.accounts.ns_add_member","args":["acme",'"$ERIN_UID"',"reporter"]}' >/dev/null
gw_rpc $ROOT_COOKIES '{"path":"accounts.accounts.ns_create","args":['"$RUID"',"group","platform","acme"]}' >/dev/null
GAPI=$(gw_rpc $ROOT_COOKIES '{"path":"git_repo.git_repo.make","args":['"$RUID"',"acme","api"]}' | rb_result)
GSVC=$(gw_rpc $ROOT_COOKIES '{"path":"git_repo.git_repo.make","args":['"$RUID"',"acme/platform","svc"]}' | rb_result)

# Re-login bob/erin so their JWTs pick up the memberships just granted.
curl -sS --max-time 10 -o /dev/null -c "$LOG_DIR/rb-bob.txt"  -XPOST "http://127.0.0.1:${WEB}/login" --data-urlencode 'username=bob'  --data-urlencode 'password=pw-bob'
curl -sS --max-time 10 -o /dev/null -c "$LOG_DIR/rb-erin.txt" -XPOST "http://127.0.0.1:${WEB}/login" --data-urlencode 'username=erin' --data-urlencode 'password=pw-erin'

# (2) Group developer can push a group repo.
bpush=$(gw_rpc $LOG_DIR/rb-bob.txt '{"path":"git_repo.git_repo.put_file","args":['"$GAPI"',"a.txt","x","c","bob","b@x"]}')
expect_contains 'rbac: group developer pushes group repo' "$bpush" '"result":"[0-9a-f]'

# (3) Group reporter can read but NOT push.
rcode=$(gw_code $LOG_DIR/rb-erin.txt '{"path":"git_repo.git_repo.read_tree","args":['"$GAPI"',"",""]}')
[ "$rcode" = "200" ] && note_pass "rbac: group reporter reads group repo (200)" \
                     || note_fail "rbac: reporter read returned $rcode (want 200)"
wcode=$(gw_code $LOG_DIR/rb-erin.txt '{"path":"git_repo.git_repo.put_file","args":['"$GAPI"',"z.txt","x","c","erin","e@x"]}')
[ "$wcode" = "403" ] && note_pass "rbac: group reporter cannot push (403)" \
                     || note_fail "rbac: reporter push returned $wcode (want 403)"

# (4) Inherited parent-namespace role applies to a subgroup repo: bob is
# developer on acme, so he pushes acme/platform/svc with no direct grant there.
ipush=$(gw_rpc $LOG_DIR/rb-bob.txt '{"path":"git_repo.git_repo.put_file","args":['"$GSVC"',"s.txt","x","c","bob","b@x"]}')
expect_contains 'rbac: inherited parent role applies to subgroup repo' "$ipush" '"result":"[0-9a-f]'

# (5) A user with no namespace role is denied.
dcode=$(gw_code $LOG_DIR/rb-dave.txt '{"path":"git_repo.git_repo.put_file","args":['"$GAPI"',"d.txt","x","c","dave","d@x"]}')
[ "$dcode" = "403" ] && note_pass "rbac: user with no namespace role denied (403)" \
                     || note_fail "rbac: no-role push returned $dcode (want 403)"

# (6) Site owner/maintainer bypass works for namespace-gated writes.
spush=$(gw_rpc $ROOT_COOKIES '{"path":"git_repo.git_repo.put_file","args":['"$GAPI"',"r.txt","x","c","root","r@x"]}')
expect_contains 'rbac: site-owner bypass pushes any repo' "$spush" '"result":"[0-9a-f]'

# (7) ns_add_member is itself role-gated: a non-member (dave) cannot grant roles.
ncode=$(gw_code $LOG_DIR/rb-dave.txt '{"path":"accounts.accounts.ns_add_member","args":["acme",'"$DAVE_UID"',"owner"]}')
[ "$ncode" = "403" ] && note_pass "rbac: non-member cannot grant namespace roles (403)" \
                     || note_fail "rbac: outsider ns_add_member returned $ncode (want 403)"

# (8) Public repos are world-readable (allow_public) — even anonymously; a
# private repo refuses an anonymous read. alice flips her repo public first.
gw_rpc $SIDE_COOKIES '{"path":"git_repo.git_repo.set_public","args":['"$ARID"',1]}' >/dev/null
anoncfg='-XPOST http://127.0.0.1:'"${WEB}"'/_rpc -H Content-Type:application/json'
pubcode=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' $anoncfg -d '{"path":"git_repo.git_repo.read_tree","args":['"$ARID"',"",""]}')
[ "$pubcode" = "200" ] && note_pass "rbac: anonymous reads a PUBLIC repo (200)" \
                       || note_fail "rbac: anonymous public read returned $pubcode (want 200)"
privcode=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' $anoncfg -d '{"path":"git_repo.git_repo.read_tree","args":['"$GAPI"',"",""]}')
[ "$privcode" = "401" ] && note_pass "rbac: anonymous denied on a PRIVATE repo (401)" \
                        || note_fail "rbac: anonymous private read returned $privcode (want 401)"

# (9) Canonical authority: a grant on a NONEXISTENT namespace is rejected, and a
# repo cannot be created under a namespace that was never created.
gcode=$(gw_code $ROOT_COOKIES '{"path":"accounts.accounts.ns_add_member","args":["ghost",'"$BOB_UID"',"developer"]}')
[ "$gcode" = "500" ] && note_pass "rbac: grant on nonexistent namespace rejected (500)" \
                     || note_fail "rbac: grant on ghost namespace returned $gcode (want 500)"
mcode=$(gw_code $ROOT_COOKIES '{"path":"git_repo.git_repo.make","args":['"$RUID"',"ghostns","r"]}')
[ "$mcode" = "500" ] && note_pass "rbac: repo under nonexistent namespace rejected (500)" \
                     || note_fail "rbac: make under ghost namespace returned $mcode (want 500)"

# (10) Claim injection: a slug with comma/colon cannot smuggle a second
# "<path>:<role>" into the groups claim — rejected by the strict slug grammar.
icode=$(gw_code $ROOT_COOKIES '{"path":"accounts.accounts.ns_create","args":['"$RUID"',"group","evil,site",""]}')
[ "$icode" = "500" ] && note_pass "rbac: comma/colon slug injection rejected (500)" \
                     || note_fail "rbac: injection slug returned $icode (want 500)"

# (11) GitLab-style root groups: with the default config
# (accounts.allow_user_root_groups true) any signed-in user may create a
# top-level group and owns it. The reserved `site` namespace stays off-limits.
scode=$(gw_code $LOG_DIR/rb-bob.txt '{"path":"accounts.accounts.ns_create","args":['"$BOB_UID"',"group","bobgroup",""]}')
[ "$scode" = "200" ] && note_pass "rbac: any user can create a root group (GitLab default, 200)" \
                     || note_fail "rbac: user root ns_create returned $scode (want 200)"
sitecode=$(gw_code $LOG_DIR/rb-bob.txt '{"path":"accounts.accounts.ns_create","args":['"$BOB_UID"',"group","site",""]}')
[ "$sitecode" = "500" ] && note_pass "rbac: reserved 'site' namespace stays off-limits to users (500)" \
                        || note_fail "rbac: user creating 'site' returned $sitecode (want 500)"

# (12) Pipeline log reads are namespace-scoped (git_pipeline enforces it): the
# repo owner can read a job's log; a no-role user cannot (private repo). JOB_ID
# was run against alice's private repo in [5d].
if [ -n "${JOB_ID:-}" ]; then
    alog=$(gw_code $SIDE_COOKIES '{"path":"git_pipeline.git_pipeline.read_log","args":['"$JOB_ID"']}')
    [ "$alog" = "200" ] && note_pass "rbac: repo owner reads a job log (200)" \
                        || note_fail "rbac: owner read_log returned $alog (want 200)"
    dlog=$(gw_code $LOG_DIR/rb-dave.txt '{"path":"git_pipeline.git_pipeline.read_log","args":['"$JOB_ID"']}')
    [ "$dlog" = "500" ] && note_pass "rbac: no-role user denied a private repo's job log (500)" \
                        || note_fail "rbac: no-role read_log returned $dlog (want 500)"
fi

# (13) Legacy lease is a runner-only operation: a signed-in non-runner cannot
# claim queued jobs (which would starve real runners).
lcode=$(gw_code $SIDE_COOKIES '{"path":"git_pipeline.git_pipeline.lease","args":[1]}')
[ "$lcode" = "403" ] && note_pass "rbac: non-runner cannot lease jobs (403)" \
                     || note_fail "rbac: non-runner lease returned $lcode (want 403)"

# (14) A signed-in user cannot enqueue a pipeline on a repo they have no role on
# (acme/api is the group repo; dave has no acme role).
ecode=$(gw_code $LOG_DIR/rb-dave.txt '{"path":"git_pipeline.git_pipeline.enqueue_job","args":['"$GAPI"',"refs/heads/x","",60]}')
[ "$ecode" = "403" ] && note_pass "rbac: no-role user cannot enqueue a pipeline (403)" \
                     || note_fail "rbac: no-role enqueue_job returned $ecode (want 403)"

# (15) Per-owner repo listings don't leak across namespaces: the owner lists
# their own repos; a different signed-in user is denied (no private-name leak).
olist=$(gw_code $SIDE_COOKIES '{"path":"git_repo.git_repo.list_for_owner","args":['"$AUID"']}')
[ "$olist" = "200" ] && note_pass "rbac: owner lists own repos (200)" \
                     || note_fail "rbac: owner list_for_owner returned $olist (want 200)"
xlist=$(gw_code $LOG_DIR/rb-dave.txt '{"path":"git_repo.git_repo.list_for_owner","args":['"$AUID"']}')
[ "$xlist" = "500" ] && note_pass "rbac: cross-namespace repo listing denied (500)" \
                     || note_fail "rbac: cross-namespace list_for_owner returned $xlist (want 500)"

# (16) Repo deletion is maintainer-gated on the repo's namespace.
twid=$(gw_rpc $SIDE_COOKIES '{"path":"git_repo.git_repo.make","args":['"$AUID"',"alice","throwaway"]}' | rb_result)
ddel=$(gw_code $LOG_DIR/rb-dave.txt '{"path":"git_repo.git_repo.delete","args":['"$twid"']}')
[ "$ddel" = "403" ] && note_pass "rbac: non-member cannot delete a repo (403)" \
                    || note_fail "rbac: non-member delete returned $ddel (want 403)"
adel=$(gw_rpc $SIDE_COOKIES '{"path":"git_repo.git_repo.delete","args":['"$twid"']}')
expect_contains 'rbac: repo owner can delete own repo' "$adel" '"result":1'

# (17) Legacy git_pipeline.complete is default-denied at the gateway (absent
# from policy) AND guarded service-local (runner-owner or maintainer+).
ccode=$(gw_code $LOG_DIR/rb-dave.txt '{"path":"git_pipeline.git_pipeline.complete","args":[1,0]}')
[ "$ccode" = "403" ] && note_pass "rbac: legacy complete default-denied at gateway (403)" \
                     || note_fail "rbac: legacy complete returned $ccode (want 403)"

# (18) token_issuer.mint is fronted UNAUTHENTICATED by the loopback bridge, so it
# must refuse to forge a privilege-granting JWT (system:internal / site:owner) —
# otherwise it's an RBAC bypass. A non-privileged runner token stays mintable.
mforge=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -XPOST "http://127.0.0.1:${BRIDGE}/_rpc" \
         -H 'Content-Type: application/json' -d '{"path":"token_issuer.token_issuer.mint","args":[9999,"x","system:internal",0]}')
[ "$mforge" = "500" ] && note_pass "rbac: mint refuses to forge a system:internal JWT (500)" \
                      || note_fail "rbac: mint system:internal via bridge returned $mforge (want 500)"
mrun=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -XPOST "http://127.0.0.1:${BRIDGE}/_rpc" \
       -H 'Content-Type: application/json' -d '{"path":"token_issuer.token_issuer.mint","args":[9999,"r","site:runner,runner:9999",0]}')
[ "$mrun" = "200" ] && note_pass "rbac: mint still issues a non-privileged runner token (200)" \
                    || note_fail "rbac: mint runner token returned $mrun (want 200)"

echo
echo "[5f] webapp admin RBAC management (issue #30) — namespaces + role members"
# The site-admin RBAC UI in the picoforge webapp ($SIDE). root (site owner) is
# signed in via $ROOT_COOKIES; the acme group + its members were provisioned in
# [5e] through the gateway, so the admin pages must surface them.
out=$(curl -sS --max-time 10 -b $ROOT_COOKIES "http://127.0.0.1:${SIDE}/-/admin/namespaces")
expect_contains 'webapp GET /admin/namespaces (RBAC admin)'  "$out" '<h1>Namespaces</h1>'
expect_contains 'admin sidebar has a Namespaces entry'       "$out" 'href="/-/admin/namespaces"'
expect_contains 'admin/namespaces lists the site namespace'  "$out" '/-/admin/namespaces/site'
expect_contains 'admin/namespaces lists the acme group'      "$out" '/-/admin/namespaces/acme'
expect_contains 'admin/namespaces has a create-group form'   "$out" 'action="/-/admin/namespaces/create"'
# Drill into the acme group: it must show its members (bob is a developer).
out=$(curl -sS --max-time 10 -b $ROOT_COOKIES "http://127.0.0.1:${SIDE}/-/admin/namespaces/acme")
expect_contains 'webapp namespace detail shows the members panel' "$out" 'Grant a role'
expect_contains 'namespace detail shows a member role'            "$out" 'developer'
# Create a group through the webapp form, then grant bob a role through it.
curl -sS --max-time 10 -o /dev/null -b $ROOT_COOKIES -XPOST "http://127.0.0.1:${SIDE}/-/admin/namespaces/create" \
     --data-urlencode 'slug=webgroup' --data-urlencode 'parent=' >/dev/null
out=$(curl -sS --max-time 10 -b $ROOT_COOKIES "http://127.0.0.1:${SIDE}/-/admin/namespaces")
expect_contains 'webapp create-group form created a namespace' "$out" '/-/admin/namespaces/webgroup'
curl -sS --max-time 10 -o /dev/null -b $ROOT_COOKIES -XPOST "http://127.0.0.1:${SIDE}/-/admin/namespaces/add_member" \
     --data-urlencode 'path=webgroup' --data-urlencode "uid=${BOB_UID}" --data-urlencode 'role=maintainer' >/dev/null
out=$(curl -sS --max-time 10 -b $ROOT_COOKIES "http://127.0.0.1:${SIDE}/-/admin/namespaces/webgroup")
expect_contains 'webapp grant-role form added a member (maintainer)' "$out" 'maintainer'
# Revoke it again through the webapp form.
curl -sS --max-time 10 -o /dev/null -b $ROOT_COOKIES -XPOST "http://127.0.0.1:${SIDE}/-/admin/namespaces/remove_member" \
     --data-urlencode 'path=webgroup' --data-urlencode "uid=${BOB_UID}" >/dev/null
out=$(curl -sS --max-time 10 -b $ROOT_COOKIES "http://127.0.0.1:${SIDE}/-/admin/namespaces/webgroup")
# After revoke the members table holds only the owner (root); bob (the only
# non-owner) is gone, so no 'maintainer' role row remains in the table.
mcount=$(printf '%s' "$out" | grep -o "maintainer" | wc -l)
[ "$mcount" -le 1 ] && note_pass "webapp revoke-role form removed the member" \
                    || note_fail "webapp revoke did not remove the member (maintainer count=$mcount)"
# A non-admin (alice) is refused the RBAC admin area.
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -b $SIDE_COOKIES "http://127.0.0.1:${SIDE}/-/admin/namespaces")
[ "$code" = "403" ] && note_pass "webapp /admin/namespaces refuses a non-admin (403)" \
                    || note_fail "non-admin /admin/namespaces returned $code (want 403)"

# Namespace-based repo discovery: the GROUP namespace page lists the group's
# repos (acme/api was created under the acme namespace in [5e]). Before this,
# the page hashed the account into a uid and found nothing for group repos.
out=$(curl -sS --max-time 10 -b $ROOT_COOKIES "http://127.0.0.1:${SIDE}/acme")
expect_contains 'webapp group namespace page lists group repos' "$out" '/acme/api'
# Repo creation offers a NAMESPACE picker (not just personal). bob is a
# developer on acme, so his /repos/new must offer the acme namespace.
out=$(curl -sS --max-time 10 -b $LOG_DIR/rb-bob.txt "http://127.0.0.1:${SIDE}/-/repos/new")
expect_contains 'webapp /repos/new has a namespace field'    "$out" 'name="namespace"'
expect_contains 'namespace picker suggests a group bob can push to' "$out" 'value="acme"'
# bob creates a repo in the acme GROUP through the webapp form.
curl -sS --max-time 10 -o /dev/null -b $LOG_DIR/rb-bob.txt -XPOST "http://127.0.0.1:${SIDE}/-/repos/new" \
     --data-urlencode 'namespace=acme' --data-urlencode 'name=bobweb' >/dev/null
out=$(curl -sS --max-time 10 -b $ROOT_COOKIES "http://127.0.0.1:${SIDE}/acme")
expect_contains 'group repo created via webapp shows on the namespace page' "$out" '/acme/bobweb'
# The group repo must list at its NAMESPACE URL on bob's own /repos page (the
# owner index stores the full path), not at /bob/bobweb.
out=$(curl -sS --max-time 10 -b $LOG_DIR/rb-bob.txt "http://127.0.0.1:${SIDE}/-/repos")
expect_contains "bob's /repos links the group repo at its namespace path" "$out" 'href="/acme/bobweb/-/tree"'
# A repo may not be named after a route word (would be unbrowseable nested).
rcode=$(gw_code $ROOT_COOKIES '{"path":"git_repo.git_repo.make","args":['"$RUID"',"acme","settings"]}')
[ "$rcode" = "500" ] && note_pass "rbac: reserved repo name 'settings' rejected (500)" \
                     || note_fail "rbac: reserved repo name returned $rcode (want 500)"
# Non-admin group management: root owns acme, so /groups lists it and the
# manage page works; a nested subgroup's repos are discoverable there. Roles are
# point-in-time JWT claims, so re-login root to pick up the acme ownership it was
# granted (via ns_create) after its earlier webapp login.
curl -sS --max-time 10 -o /dev/null -c $ROOT_COOKIES -XPOST "http://127.0.0.1:${SIDE}/-/login" \
     --data-urlencode 'username=root' --data-urlencode 'password=rootpw' >/dev/null
out=$(curl -sS --max-time 10 -b $ROOT_COOKIES "http://127.0.0.1:${SIDE}/-/groups")
expect_contains 'webapp /groups lists a namespace the user owns' "$out" '/-/groups/acme'
out=$(curl -sS --max-time 10 -b $ROOT_COOKIES "http://127.0.0.1:${SIDE}/-/groups/acme/platform")
expect_contains 'webapp /groups subgroup page lists nested repos' "$out" 'acme/platform/svc'
# Nested-namespace repos are NAVIGABLE: /acme/platform/svc resolves to repo
# 'svc' in namespace 'acme/platform' (not parsed as acct=acme/repo=platform).
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -b $ROOT_COOKIES "http://127.0.0.1:${SIDE}/acme/platform/svc/-/tree")
[ "$code" = "200" ] && note_pass "webapp navigates to a nested-namespace repo (200)" \
                    || note_fail "nested repo /acme/platform/svc returned $code (want 200)"
# A non-maintainer (alice) is refused a group management page.
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -b $SIDE_COOKIES "http://127.0.0.1:${SIDE}/-/groups/acme")
[ "$code" = "403" ] && note_pass "webapp /groups/<ns> refuses a non-maintainer (403)" \
                    || note_fail "non-maintainer /groups/acme returned $code (want 403)"

# /repos is RBAC-based discovery, NOT creator-index-based (issue #30): erin is
# only a REPORTER on acme and created nothing, yet her Projects page must list
# acme/api (created by root) because she holds a role on its namespace.
curl -sS --max-time 10 -o /dev/null -c "$LOG_DIR/web-erin.txt" -XPOST "http://127.0.0.1:${SIDE}/-/login" \
     --data-urlencode 'username=erin' --data-urlencode 'password=pw-erin' >/dev/null
out=$(curl -sS --max-time 10 -b "$LOG_DIR/web-erin.txt" "http://127.0.0.1:${SIDE}/-/repos")
expect_contains 'webapp /repos shows a group repo the user can access but did not create' "$out" 'href="/acme/api/-/tree"'

# /repos follows INHERITED role into subgroups: bob is developer on acme with NO
# direct grant on acme/platform, yet his Projects page must list the subgroup
# repo acme/platform/svc (discovered via the namespace subtree, not the creator
# index). This is the case the creator-index discovery missed.
curl -sS --max-time 10 -o /dev/null -c "$LOG_DIR/web-bob.txt" -XPOST "http://127.0.0.1:${SIDE}/-/login" \
     --data-urlencode 'username=bob' --data-urlencode 'password=pw-bob' >/dev/null
out=$(curl -sS --max-time 10 -b "$LOG_DIR/web-bob.txt" "http://127.0.0.1:${SIDE}/-/repos")
expect_contains 'webapp /repos lists an INHERITED subgroup repo (no direct grant, not creator)' "$out" 'href="/acme/platform/svc/-/tree"'

# A member SEES every namespace they belong to on /groups, whatever the role —
# erin is only a REPORTER on acme, yet /groups must list acme. This is the
# "added to a group but the member can't see it anywhere" gap.
out=$(curl -sS --max-time 10 -b "$LOG_DIR/web-erin.txt" "http://127.0.0.1:${SIDE}/-/groups")
expect_contains 'webapp /groups lists a namespace where the user is a non-maintainer member' "$out" '>acme</a>'

echo
# Persistence proof. The security/rel-db refactor split storage in two:
#   * Account + session state (users, username→uid, sessions, tokens) moved
#     to the RELATIONAL clusters — per-shard SQLite under $ROOT/rel/{uid,
#     username,session,token} — where it lives in real tables with columns.
#   * The sharded_storage (mdbx) backend keeps KV-shaped runtime state; after
#     the move it holds only the `count` counters, NOT user:/uid:/groups: keys.
# So the real account-persistence proof is now the relational store: the two
# registered users (root, alice) must be on disk WITH their usernames, and
# their sessions must be persisted. We read the SQLite shards directly (Python
# stdlib sqlite3 — no CLI dep), the authoritative check rather than grepping
# raw mdbx pages for keys that no longer live there.
echo "[6/6] persistence proof — relational store (SQLite shards) must hold users + sessions"
REL_DIR=$ROOT/rel
persisted=$(python3 - "$REL_DIR" <<'PY'
import sys, sqlite3, glob, os
rel = sys.argv[1]
def rows(cluster, query):
    out = []
    for db in glob.glob(os.path.join(rel, cluster, "shard_*.db")):
        try:
            con = sqlite3.connect("file:%s?mode=ro" % db, uri=True)
            out += con.execute(query).fetchall()
            con.close()
        except Exception:
            pass
    return out
users = {u for (u,) in rows("uid", "SELECT username FROM users") if u}
sessions = rows("session", "SELECT uid FROM sessions")
# alice + root registered above; both must be on disk with a non-empty username,
# and at least their sessions must be persisted.
print("OK" if {"root", "alice"} <= users and len(sessions) >= 2
      else "NO users=%s sessions=%d" % (sorted(users), len(sessions)))
PY
)
if [ "$persisted" = "OK" ]; then
    note_pass "relational store on disk has registered users (root, alice) + persisted sessions"
else
    note_fail "relational store missing expected users/sessions — $persisted"
fi
du -sh "$REL_DIR" 2>/dev/null | sed 's/^/  relational on-disk size: /'
# sharded_storage (mdbx) should still hold the KV-shaped counters it now owns.
shard_dats=$(ls "$ROOT"/sharded/shard-*/mdbx.dat 2>/dev/null)
if [ -n "$shard_dats" ] && grep -aq "count" $shard_dats 2>/dev/null; then
    note_pass "sharded mdbx on disk has KV counters (count)"
else
    note_fail "sharded mdbx counters not found on disk"
fi

echo
echo "========================================"
echo "PASS: $PASS    FAIL: $FAIL"
echo "========================================"
if [ "$FAIL" -gt 0 ]; then
    echo "parent log: $PARENT_LOG"
    exit 1
fi
echo ""
echo "OK — mesh is live. parent log: $PARENT_LOG"
echo "    Browser (picoforge-webapp — the page tier):"
echo "      open http://127.0.0.1:${SIDE}/-/login   (server-side HTML)"
echo "    Gateway (API only — /_rpc, /_describe; HTML 404s here):"
echo "      curl http://127.0.0.1:${WEB}/_describe"
echo "    Control plane:"
echo "      curl http://127.0.0.1:${CTRL}/        (mesh REST)"
echo "    Service console (gh#15 — generic, app-agnostic):"
echo "      open http://127.0.0.1:${CONSOLE}/_alpine    (console -> yhttp bridge :${BRIDGE} -> yrpc backends)"
echo "    Tracing:"
echo "      picotrace is mesh-managed: resolve service picotrace in the registry"
echo "      curl http://127.0.0.1:${WEB}/_perf    (local op-latency aggregate)"
echo "      curl -XPOST http://127.0.0.1:${WEB}/_rpc -d '{\"path\":\"trace_collector.trace_collector.services\",\"args\":[]}'"
echo "                                            (trace collector via gateway — see docs/tracing.md)"
echo "    Press Ctrl-C to tear everything down."

# Honour the message above: stay alive until the user kills us, or until
# the parent picomesh dies on its own. Without this the script exits and
# the EXIT trap SIGKILLs the parent — the mesh would look "crashed" to
# anyone trying to open the URL.
#
# `wait $PID` is documented to be interruptible by traps, but in practice
# bash only fires the trap after the awaited PID exits — so SIGINT from
# the terminal stays queued and the user thinks Ctrl-C is dead. A poll
# loop with a short sleep gives the trap a chance to fire promptly.
while kill -0 "$PARENT" 2>/dev/null; do
    sleep 1
done
