#!/usr/bin/env bash
# Run the mesh-up.sh assertion battery against the riscv64 yaafc
# running inside qemu, reached over the slirp hostfwd at
# 127.0.0.1:18080. Same shape as scenarios/git-yaafc/mesh-up.sh
# (those tests are the source of truth) but stripped of the local
# parent-spawn / mesh-store-create steps — qemu's run.sh already
# brought the mesh up.
#
# Boot a fresh qemu first so we start from clean storage (alice
# doesn't exist yet, /register tests can assert "creates" then
# "duplicate"). Tear it down at the end.
#
# Run from repo root.

set -uo pipefail

REPO="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$REPO"

WEB=18080
LOG_DIR="$REPO/build-yemu-release"
mkdir -p "$LOG_DIR"
QEMU_LOG="$LOG_DIR/test-qemu.qemu.log"
COOKIES="$LOG_DIR/test-qemu-cookies.txt"
BOB_COOKIES="$LOG_DIR/test-qemu-bob-cookies.txt"

PASS=0
FAIL=0
note_pass() { PASS=$((PASS+1)); echo "  PASS: $1"; }
note_fail() { FAIL=$((FAIL+1)); echo "  FAIL: $1" >&2; }

http_get() {
    curl -sS --max-time 10 -b "$COOKIES" -c "$COOKIES" \
         "http://127.0.0.1:$WEB$1"
}
http_post_form() {
    curl -sS --max-time 10 -b "$COOKIES" -c "$COOKIES" \
         -XPOST "http://127.0.0.1:$WEB$1" "${@:2}"
}
hdrs_get() {
    curl -sS --max-time 10 -D - -o /dev/null \
         -b "$COOKIES" -c "$COOKIES" \
         "http://127.0.0.1:$WEB$1"
}
hdrs_post_form() {
    curl -sS --max-time 10 -D - -o /dev/null \
         -b "$COOKIES" -c "$COOKIES" \
         -XPOST "http://127.0.0.1:$WEB$1" "${@:2}"
}
expect_contains() {
    local label=$1 resp=$2 needle=$3
    if [[ "$resp" =~ $needle ]]; then note_pass "$label"
    else note_fail "$label — got: ${resp:0:200}"; fi
}

# ---------------------------------------------------------------- boot

echo "[1/4] killing any existing qemu, then booting fresh"
pkill -9 -f qemu-system-riscv64 2>/dev/null || true
sleep 1
: > "$QEMU_LOG"
# -snapshot makes qemu hold all disk writes in RAM only — alpine-rootfs.img
# is never touched. Each test run therefore starts from the same clean
# image (alice doesn't exist, no leftover sessions). Without this the
# register-then-login assertions break on the second run because alice
# was created by the first.
SMP=2 MEM=512M bash "$REPO/scenarios/git-yaafc/yemu/run-vm.sh" -snapshot \
    < /dev/null > "$QEMU_LOG" 2>&1 &
QPID=$!
cleanup() {
    if kill -0 "$QPID" 2>/dev/null; then
        kill -KILL "$QPID" 2>/dev/null || true
    fi
}
trap cleanup EXIT
echo "  qemu pid=$QPID"

echo "[2/4] waiting up to 120s for gateway :8080 → host :$WEB to accept"
deadline=$(( $(date +%s) + 120 ))
while [ $(date +%s) -lt $deadline ]; do
    if curl -sf --max-time 2 "http://127.0.0.1:$WEB/login" >/dev/null 2>&1; then
        echo "  gateway up"
        break
    fi
    sleep 2
done
if ! curl -sf --max-time 5 "http://127.0.0.1:$WEB/login" >/dev/null; then
    echo "FAIL: gateway never came up. Tail of qemu log:"
    tail -40 "$QEMU_LOG"
    exit 1
fi

# ---------------------------------------------------------------- tests

echo "[3/4] running test battery against http://127.0.0.1:$WEB"
rm -f "$COOKIES" "$BOB_COOKIES"
touch "$COOKIES"

# --- Anonymous landing page.
out=$(http_get /login)
expect_contains 'GET /login renders sign-in card'   "$out" '<h1>Sign in</h1>'
expect_contains 'GET /login shows username field'   "$out" 'name="username"'
expect_contains 'GET /login shows password field'   "$out" 'name="password"'
expect_contains 'GET /login pulls htmx'             "$out" 'unpkg.com/htmx.org'
expect_contains 'GET /style.css served by gateway'  "$(http_get /style.css)" 'minimal, github-inspired'

# --- Anon visitor on a protected page → 303 /login.
hdrs=$(hdrs_get /repos)
expect_contains 'GET /repos as anon → 303 /login' "$hdrs" '303 See Other.*[Ll]ocation: /login|[Ll]ocation: /login.*303'

# --- Cold sign-in fails for unregistered user.
out=$(curl -sS --max-time 10 -XPOST "http://127.0.0.1:$WEB/login" \
           --data-urlencode 'username=alice' --data-urlencode 'password=hunter2')
expect_contains 'POST /login on unregistered user fails' "$out" 'no such user'

# --- Register alice → 303 /alice + cookies.
hdrs=$(hdrs_post_form /register \
        --data-urlencode 'username=alice' --data-urlencode 'password=hunter2')
expect_contains 'POST /register → 303 /alice'      "$hdrs" '303 See Other.*[Ll]ocation: /alice|[Ll]ocation: /alice.*303'
expect_contains 'POST /register sets sid cookie'   "$hdrs" 'Set-Cookie: yaafc-sid='
expect_contains 'POST /register sets uname cookie' "$hdrs" 'Set-Cookie: yaafc-uname=alice'

# --- Duplicate /register rejected.
out=$(curl -sS --max-time 10 -XPOST "http://127.0.0.1:$WEB/register" \
           --data-urlencode 'username=alice' --data-urlencode 'password=hunter2')
expect_contains 'POST /register rejects duplicate' "$out" 'already taken'

# --- Logout invalidates session + wipes cookies.
hdrs=$(hdrs_post_form /logout)
expect_contains 'POST /logout → 303'              "$hdrs" '303 See Other'
expect_contains 'POST /logout clears sid cookie'  "$hdrs" 'yaafc-sid=;'
expect_contains 'POST /logout clears uname cookie' "$hdrs" 'yaafc-uname=;'

# --- Re-login.
hdrs=$(curl -sS --max-time 10 -D - -o /dev/null -c "$COOKIES" \
            -XPOST "http://127.0.0.1:$WEB/login" \
            --data-urlencode 'username=alice' --data-urlencode 'password=hunter2')
expect_contains 'POST /login → 303 /alice'   "$hdrs" '303 See Other.*[Ll]ocation: /alice|[Ll]ocation: /alice.*303'
expect_contains 'POST /login sets sid cookie' "$hdrs" 'Set-Cookie: yaafc-sid='

# --- Create a repo.
hdrs=$(hdrs_post_form /repos/new --data-urlencode 'name=website')
expect_contains 'POST /repos/new → 303 /alice/website' "$hdrs" '303 See Other.*/alice/website|/alice/website.*303'

# --- Authenticated pages.
out=$(http_get /repos);              expect_contains 'GET /repos'              "$out" '<h1>Repositories</h1>'
out=$(http_get /alice);              expect_contains 'GET /alice'              "$out" '<h1>@alice</h1>'
out=$(http_get /alice/website);      expect_contains 'GET /alice/website'      "$out" 'alice</a>/website'
out=$(http_get /alice/website/issues); expect_contains 'GET /alice/website/issues' "$out" '<h1>Issues</h1>'
out=$(http_get /alice/website/runs);   expect_contains 'GET /alice/website/runs'   "$out" 'Pipeline'
out=$(http_get /admin/users);          expect_contains 'GET /admin/users (site owner)' "$out" '<h1>Users</h1>'

# --- Non-owner cannot reach /admin/users.
curl -sS --max-time 10 -c "$BOB_COOKIES" \
     -XPOST "http://127.0.0.1:$WEB/register" \
     --data-urlencode 'username=bob' --data-urlencode 'password=bobsecret' \
     -o /dev/null
hdrs=$(curl -sS --max-time 10 -D - -o /dev/null -b "$BOB_COOKIES" \
            "http://127.0.0.1:$WEB/admin/users")
expect_contains 'non-owner /admin/users → 303 /bob' "$hdrs" '303 See Other.*[Ll]ocation: /bob|[Ll]ocation: /bob.*303'

# --- Open an issue via the HTML form.
hdrs=$(hdrs_post_form /alice/website/issues/new)
expect_contains 'POST /alice/website/issues/new → 303' "$hdrs" '303 See Other'

# ---------------------------------------------------------------- summary
echo
echo "[4/4] summary"
echo "========================================"
echo "PASS: $PASS    FAIL: $FAIL"
echo "========================================"
if [ "$FAIL" -gt 0 ]; then
    echo "qemu log: $QEMU_LOG"
    exit 1
fi
echo "OK — all assertions passed against qemu @ 127.0.0.1:$WEB"
