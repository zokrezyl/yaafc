#!/usr/bin/env bash
# picoforge — bring up the full mesh and prove it works end-to-end.
#
# Layout:
#
#   parent picomesh --frontend yhttp --port 8800        ← control channel (REST)
#     ↳ mesh_store_reconcile_from_config spawns one child per service
#       in mesh.services.* on the service's own port. Each backend
#       child uses --frontend yrpc (binary, for inter-service RPC).
#       The 'gateway' service (port 8080) overrides this with
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

PICOMESH=./build-desktop-release/picomesh
WEBAPP=./build-desktop-release/picoforge-webapp
CONFIG=scenarios/picoforge/picoforge.yaml
CTRL=8800
WEB=8080
SIDE=8081
DB=/tmp/picoforge/central.db

# Start from a clean slate. Wipe the ENTIRE /tmp/picoforge tree, not just
# central.db — the backends persist accounts/sessions in the sharded mdbx
# store at /tmp/picoforge/sharded/, so leaving it behind makes a re-run
# fail: `alice`/`bob` from the previous run still exist, so POST /register
# returns "username already taken" (HTTP 200) instead of 303, and the
# login/cookie assertions cascade. Removing the whole tree makes every
# run reproducible.
rm -rf /tmp/picoforge
mkdir -p tmp /tmp/picoforge
rm -f "$DB" tmp/mesh-parent.log

PASS=0
FAIL=0
note_pass() { PASS=$((PASS+1)); echo "  PASS: $1"; }
note_fail() { FAIL=$((FAIL+1)); echo "  FAIL: $1" >&2; }

echo "[1/6] starting parent picomesh (yhttp control) on :${CTRL}"
"$PICOMESH" --config-file "$CONFIG" --frontend yhttp --host 127.0.0.1 --port "$CTRL" serve \
    > tmp/mesh-parent.log 2>&1 &
PARENT=$!
cleanup() {
    pkill -9 -f 'picoforge-webapp' 2>/dev/null || true
    pkill -9 -f 'picomesh.*serve' 2>/dev/null || true
    kill -KILL $PARENT 2>/dev/null || true
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
    curl -sS --max-time 10 -b tmp/cookies.txt -c tmp/cookies.txt -XPOST \
         "http://127.0.0.1:$port$path" "$@"
}
http_get() {
    local port=$1 path=$2
    curl -sS --max-time 10 -b tmp/cookies.txt -c tmp/cookies.txt \
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

echo "[2/6] create mesh_store on parent"
out=$(http_post "$CTRL" /create '{"class":"mesh_store"}')
H=$(echo "$out" | sed -E 's/.*"handle":([0-9]+).*/\1/')
expect_contains 'mesh_store create' "$out" '"handle":[0-9]+'

echo "[3/6] mesh.reconcile_from_config — spawn every service as a child"
out=$(http_post "$CTRL" /invoke "{\"method\":\"mesh_store_reconcile_from_config\",\"handle\":$H,\"args\":[]}")
expect_contains 'reconcile spawn count > 0' "$out" '"result":[1-9][0-9]*'
SPAWNED=$(echo "$out" | sed -E 's/.*"result":([0-9]+).*/\1/')
echo "  spawned: $SPAWNED children"

echo "[4/6] waiting for children to bind…"
sleep 1.5

echo "[5/6] gateway is API + auth-action only (NO HTML pages) on :${WEB}"
rm -f tmp/cookies.txt
touch tmp/cookies.txt

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
hdrs=$(curl -sS --max-time 10 -D - -o /dev/null -c tmp/cookies.txt \
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
hdrs=$(curl -sS --max-time 10 -D - -o /dev/null -b tmp/cookies.txt -c tmp/cookies.txt \
            -XPOST http://127.0.0.1:$WEB/logout)
expect_contains 'POST /logout → 303 /login'        "$hdrs" '303 See Other'
expect_contains 'POST /logout clears sid cookie'   "$hdrs" 'picomesh-sid=;'
expect_contains 'POST /logout clears uname cookie' "$hdrs" 'picomesh-uname=;'

# Re-login (now that the account exists from /register).
hdrs=$(curl -sS --max-time 10 -D - -o /dev/null -c tmp/cookies.txt \
            -XPOST http://127.0.0.1:$WEB/login \
            --data-urlencode 'username=alice' --data-urlencode 'password=hunter2')
expect_contains 'POST /login → 303 /alice'   "$hdrs" '303 See Other.*[Ll]ocation: /alice|[Ll]ocation: /alice.*303'
expect_contains 'POST /login sets sid cookie' "$hdrs" 'Set-Cookie: picomesh-sid='

# Create a repo under /alice/website via /repos/new (POST `name`) — an
# authenticated action POST the gateway forwards to the git_repo backend.
hdrs=$(curl -sS --max-time 10 -D - -o /dev/null -b tmp/cookies.txt \
            -XPOST http://127.0.0.1:$WEB/repos/new \
            --data-urlencode 'name=website')
expect_contains 'POST /repos/new → 303 /alice/website' "$hdrs" '303 See Other.*/alice/website|/alice/website.*303'

# The repo's data is reachable via the gateway API (/_rpc) — this is how
# the webapp sources the page the gateway no longer renders.
out=$(curl -sS --max-time 10 -XPOST http://127.0.0.1:$WEB/_rpc \
           -H 'Content-Type: application/json' \
           -d '{"path":"git_repo.store.count_total","args":[]}')
expect_contains 'gateway /_rpc git_repo.count_total (repo data via API)' "$out" '"result":[1-9][0-9]*'

# Parent control still alive.
out=$(http_post "$CTRL" /invoke "{\"method\":\"mesh_store_count_children\",\"handle\":$H,\"args\":[]}")
expect_contains "parent.count_children == $SPAWNED" "$out" "\"result\":$SPAWNED"

echo
echo "[5b] standalone picoforge-webapp → gateway /_rpc on :${SIDE}"
# The separated browser webapp: links no plugins, knows no backend
# ports — every call leaves it as POST /_rpc against the gateway. This
# proves the chosen architecture (browser → picoforge-webapp → gateway →
# backends), distinct from the gateway serving its own HTML above.
"$WEBAPP" --gateway-url "http://127.0.0.1:${WEB}" \
    --host 127.0.0.1 --port "$SIDE" \
    --static scenarios/picoforge/webapp/static \
    > tmp/sidecar.log 2>&1 &
SIDECAR=$!
sleep 0.7

# GET sidecar /login renders its own sign-in form (not the gateway's).
out=$(curl -sS --max-time 10 "http://127.0.0.1:${SIDE}/login")
expect_contains 'sidecar GET /login renders' "$out" '<h1>Sign in</h1>'

# The /login page links to /register, so the webapp MUST serve it (this
# was a 404 hole). GET renders the form; POST relays to the gateway and,
# for a brand-new account, returns 303 + Set-Cookie like /login does.
out=$(curl -sS --max-time 10 "http://127.0.0.1:${SIDE}/register")
expect_contains 'webapp GET /register renders' "$out" '<h1>Create account</h1>'
hdrs=$(curl -sS --max-time 10 -D - -o /dev/null \
            -XPOST "http://127.0.0.1:${SIDE}/register" \
            --data-urlencode 'username=carol' --data-urlencode 'password=hunter2')
expect_contains 'webapp POST /register (new user) → 303 /repos' "$hdrs" '303 See Other.*[Ll]ocation: /repos|[Ll]ocation: /repos.*303'
expect_contains 'webapp POST /register relays sid cookie' "$hdrs" 'Set-Cookie: picomesh-sid='

# POST sidecar /login forwards the form to the gateway's composite
# login; the gateway authenticates alice (registered above), mints a
# session and answers 303 + Set-Cookie, which the sidecar relays.
rm -f tmp/side-cookies.txt
hdrs=$(curl -sS --max-time 10 -D - -o /dev/null -c tmp/side-cookies.txt \
            -XPOST "http://127.0.0.1:${SIDE}/login" \
            --data-urlencode 'username=alice' --data-urlencode 'password=hunter2')
expect_contains 'sidecar POST /login → 303 /repos' "$hdrs" '303 See Other.*[Ll]ocation: /repos|[Ll]ocation: /repos.*303'
expect_contains 'sidecar relays gateway sid cookie' "$hdrs" 'Set-Cookie: picomesh-sid='

# GET sidecar /repos renders a data page sourced from the gateway via
# /_rpc (git_repo.store.count_total) — the sidecar holds no plugins.
out=$(curl -sS --max-time 10 -b tmp/side-cookies.txt "http://127.0.0.1:${SIDE}/repos")
expect_contains 'sidecar GET /repos renders'           "$out" '<h1>Repositories</h1>'
expect_contains 'sidecar /repos sourced via gateway /_rpc' "$out" 'via the gateway'

# The full page set the gateway no longer serves now renders on the webapp,
# each driven by a live service discovered from the gateway's /_describe and
# sourced via /_rpc. These are the M4 pages: account landing, issues,
# pipeline runs, admin users.
out=$(curl -sS --max-time 10 -b tmp/side-cookies.txt "http://127.0.0.1:${SIDE}/alice")
expect_contains 'webapp GET /<account> (account landing)' "$out" '<h1>alice</h1>'
# Repo-scoped pages carry the repo name as <h1> (project header) and the
# section name in the panel header + the active project tab.
out=$(curl -sS --max-time 10 -b tmp/side-cookies.txt "http://127.0.0.1:${SIDE}/alice/website/issues")
expect_contains 'webapp GET /<acct>/<repo>/issues'        "$out" '<strong>Issues</strong>'
expect_contains 'webapp issues page active tab'           "$out" 'class="active" href="/alice/website/issues"'
out=$(curl -sS --max-time 10 -b tmp/side-cookies.txt "http://127.0.0.1:${SIDE}/alice/website/runs")
expect_contains 'webapp GET /<acct>/<repo>/runs'          "$out" '<strong>Pipeline runs</strong>'
# The Admin area is its OWN section with its OWN left menu (distinct from
# the project nav). /admin is the overview; each aspect is a page; every
# admin page shows the "Admin area" sidebar, not the project sidebar.
out=$(curl -sS --max-time 10 -b tmp/side-cookies.txt "http://127.0.0.1:${SIDE}/admin")
expect_contains 'webapp GET /admin (overview)'            "$out" '<h1>Admin</h1>'
expect_contains 'admin area has its own sidebar'          "$out" 'Admin area'
expect_contains 'admin overview links to Repositories'    "$out" 'href="/admin/repos"'
out=$(curl -sS --max-time 10 -b tmp/side-cookies.txt "http://127.0.0.1:${SIDE}/admin/users")
expect_contains 'webapp GET /admin/users'                 "$out" '<h1>Users</h1>'
expect_contains 'admin/users uses admin sidebar'          "$out" 'class="active" href="/admin/users"'
expect_contains 'admin sidebar excludes Projects nav'     "$out" 'href="/admin/services"'
out=$(curl -sS --max-time 10 -b tmp/side-cookies.txt "http://127.0.0.1:${SIDE}/admin/repos")
expect_contains 'webapp GET /admin/repos'                 "$out" '<h1>Repositories</h1>'
out=$(curl -sS --max-time 10 -b tmp/side-cookies.txt "http://127.0.0.1:${SIDE}/admin/tokens")
expect_contains 'webapp GET /admin/tokens'                "$out" '<h1>Tokens</h1>'

# New GitLab-like shell additions (gh#10): repo settings tab, services
# health page, cross-repo dashboards, and the dedicated new-repo form —
# all sourced from the gateway, all inside the same shell.
out=$(curl -sS --max-time 10 -b tmp/side-cookies.txt "http://127.0.0.1:${SIDE}/alice/website/settings")
expect_contains 'webapp GET /<acct>/<repo>/settings'      "$out" 'class="active" href="/alice/website/settings"'
out=$(curl -sS --max-time 10 -b tmp/side-cookies.txt "http://127.0.0.1:${SIDE}/admin/services")
expect_contains 'webapp GET /admin/services lists services' "$out" 'service-table'
expect_contains 'webapp /admin/services shows git_repo'   "$out" 'git_repo'
out=$(curl -sS --max-time 10 -b tmp/side-cookies.txt "http://127.0.0.1:${SIDE}/dashboard/issues")
expect_contains 'webapp GET /dashboard/issues'            "$out" 'Open issues across your repositories'
out=$(curl -sS --max-time 10 -b tmp/side-cookies.txt "http://127.0.0.1:${SIDE}/dashboard/runs")
expect_contains 'webapp GET /dashboard/runs'              "$out" 'pipeline-table'
out=$(curl -sS --max-time 10 -b tmp/side-cookies.txt "http://127.0.0.1:${SIDE}/repos/new")
expect_contains 'webapp GET /repos/new (form)'            "$out" '<h1>New repository</h1>'
# Every signed-in page wears the same shell (topbar + sidebar).
expect_contains 'webapp pages share the shell'            "$out" 'class="sidebar-nav"'

# Same pages refuse on the GATEWAY — it serves no HTML (B1 / gh#5 invariant).
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' "http://127.0.0.1:${WEB}/admin/users")
[ "$code" = "404" ] && note_pass "gateway refuses GET /admin/users (404 — page is the webapp's)" \
                     || note_fail "gateway GET /admin/users returned $code (want 404)"

# Direct gateway-API checks: yaapp-style surface present, legacy gone.
out=$(curl -sS --max-time 10 -XPOST "http://127.0.0.1:${WEB}/_rpc" \
           -H 'Content-Type: application/json' \
           -d '{"path":"git_repo.store.count_total","args":[]}')
expect_contains 'gateway POST /_rpc {path,args}' "$out" '"result":'

# Authenticated /_rpc round-trip with a real positional arg + result:
# resolve alice's session (the sid the sidecar just stored) back to her
# uid via session.store.lookup. Proves args marshalling, the remote
# forward, and a meaningful non-zero return — not just a 0 count.
ASID=$(awk '/picomesh-sid/ {print $NF}' tmp/side-cookies.txt 2>/dev/null | tail -1)
out=$(curl -sS --max-time 10 -XPOST "http://127.0.0.1:${WEB}/_rpc" \
           -H 'Content-Type: application/json' \
           -d "{\"path\":\"session.store.lookup\",\"args\":[${ASID:-0}]}")
expect_contains 'gateway /_rpc authed round-trip (session.lookup→uid)' "$out" '"result":[1-9][0-9]*'

# Malformed path → stable 400; unknown method → 404.
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -XPOST "http://127.0.0.1:${WEB}/_rpc" \
            -H 'Content-Type: application/json' -d '{"path":"oneword","args":[]}')
[ "$code" = "400" ] && note_pass "gateway /_rpc malformed path → 400" \
                     || note_fail "gateway /_rpc malformed path returned $code (want 400)"
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' -XPOST "http://127.0.0.1:${WEB}/_rpc" \
            -H 'Content-Type: application/json' -d '{"path":"git_repo.store.no_such_method","args":[]}')
[ "$code" = "404" ] && note_pass "gateway /_rpc unknown method → 404" \
                     || note_fail "gateway /_rpc unknown method returned $code (want 404)"

# _describe contract: root lists services, class form lists methods.
out=$(curl -sS --max-time 10 "http://127.0.0.1:${WEB}/_describe")
expect_contains 'gateway GET /_describe lists services' "$out" '"services":\['
out=$(curl -sS --max-time 10 "http://127.0.0.1:${WEB}/git_repo.store/_describe")
expect_contains 'gateway /<class>/_describe lists methods' "$out" 'git_repo_store_count_total'

# Legacy public surface retired on the gateway (8080)…
code=$(curl -sS --max-time 10 -o /dev/null -w '%{http_code}' \
            -XPOST "http://127.0.0.1:${WEB}/create" -d '{"class":"git_repo_store"}')
[ "$code" = "410" ] && note_pass "gateway retired POST /create (410)" \
                     || note_fail "gateway POST /create returned $code (want 410)"
# …but the mesh control parent (8800) MUST still serve it for bootstrap.
out=$(http_post "$CTRL" /create '{"class":"mesh_store"}')
expect_contains 'control parent :8800 still serves /create' "$out" '"handle":[0-9]+'

kill -KILL $SIDECAR 2>/dev/null || true

echo
# Storage migrated from the single-env sqlite `storage` plugin to the
# write-parallel `sharded_storage` (mdbx) backend — see picoforge.yaml. The
# persistence proof checks the live backend: the register/login flow
# writes keys whose raw bytes land in the mdbx shard files (no mdbx CLI
# ships here, so we grep the raw pages — crude but a true on-disk proof).
echo "[6/6] persistence proof — sharded_storage (mdbx) must have written state to disk"
SHARDED_DIR=/tmp/picoforge/sharded
shard_dats=$(ls "$SHARDED_DIR"/shard-*/mdbx.dat 2>/dev/null)
if [ -z "$shard_dats" ]; then
    note_fail "no mdbx shard files under $SHARDED_DIR (sharded_storage didn't persist?)"
else
    have_key() { grep -aq "$1" $shard_dats 2>/dev/null; }
    # accounts → user:/role:/count, session → next_sid, password_authn → count
    if have_key next_sid && have_key "user:" && have_key "role:"; then
        note_pass "sharded mdbx on disk has session + account state (next_sid, user:, role:)"
    else
        present=$(for k in next_sid count "user:" "role:" "uid:"; do have_key "$k" && printf '%s ' "$k"; done)
        note_fail "expected keys not all found in mdbx shards on disk (present: ${present:-none})"
    fi
    du -sh "$SHARDED_DIR" 2>/dev/null | sed 's/^/  sharded on-disk size: /'
fi

echo
echo "========================================"
echo "PASS: $PASS    FAIL: $FAIL"
echo "========================================"
if [ "$FAIL" -gt 0 ]; then
    echo "parent log: tmp/mesh-parent.log"
    exit 1
fi
echo ""
echo "OK — mesh is live. parent log: tmp/mesh-parent.log"
echo "    Browser:"
echo "      open http://127.0.0.1:${WEB}/login   (server-side HTML)"
echo "    Control plane:"
echo "      curl http://127.0.0.1:${CTRL}/        (mesh REST)"
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
