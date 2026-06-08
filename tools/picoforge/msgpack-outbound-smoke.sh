#!/usr/bin/env bash
# Outbound msgpack smoke: a FOREIGN (non-picomesh) Python msgpack service
# implements the calculator contract; picomesh C calls OUT to it over the
# `--transport msgpack` client path. Proves the reverse direction. Isolated +
# self-reaped via `timeout` — nothing killed by pid.
#
# Run from the repo root after `make build-desktop-release`.

set -uo pipefail

# Error-only tracing on by default (inherited by the picomesh client process).
# Override: YTRACE_LOG_LEVEL=trace for full tracing, YTRACE_DEFAULT_ON=no to mute.
export YTRACE_DEFAULT_ON="${YTRACE_DEFAULT_ON:-yes}"
export YTRACE_LOG_LEVEL="${YTRACE_LOG_LEVEL:-error}"

PICOMESH=./build-desktop-release/picomesh
# The foreign service is built on the GENERATED server skeleton
# (bindings/python/calculator_server.py via msgpack-codegen --role server, gh#23);
# only the four arithmetic bodies are hand-written.
SERVER=./tools/msgpack-client/calculator_server_impl.py
HOST=127.0.0.1
PORT=7912
LIFETIME=25

mkdir -p tmp
PASS=0
FAIL=0
pass() { PASS=$((PASS+1)); echo "  PASS: $1"; }
fail() { FAIL=$((FAIL+1)); echo "  FAIL: $1  --  $2" >&2; }
expect() { case "$3" in *"$2"*) pass "$1" ;; *) fail "$1" "$3" ;; esac; }

echo "[1/2] starting FOREIGN python msgpack service on :$PORT"
timeout "$LIFETIME" "$SERVER" "$HOST" "$PORT" > tmp/msgpack-foreign.log 2>&1 &
SRV=$!
up=0
for _ in $(seq 1 60); do
    if (exec 3<>"/dev/tcp/$HOST/$PORT") 2>/dev/null; then exec 3>&- 3<&-; up=1; break; fi
    sleep 0.25
done
[ "$up" = 1 ] && pass "foreign server up on :$PORT" || fail "foreign server never came up" "tmp/msgpack-foreign.log"

echo "[2/2] picomesh C client -> foreign msgpack service"
out=$("$PICOMESH" --plugins calculator --transport msgpack --host "$HOST" --port "$PORT" client 2>&1)
echo "$out" | sed 's/^/    /'
expect "outbound calculator.calc.add(6,7) == 13" "6 + 7 = 13" "$out"
expect "outbound calculator.calc.mul(6,7) == 42" "6 * 7 = 42" "$out"

echo
echo "========================================"
echo "PASS: $PASS    FAIL: $FAIL"
echo "========================================"
wait "$SRV" 2>/dev/null
[ "$FAIL" -eq 0 ]
