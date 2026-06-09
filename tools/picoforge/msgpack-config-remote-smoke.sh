#!/usr/bin/env bash
# #22 smoke: a picomesh C node reaches a FOREIGN msgpack service declared as a
# CONFIG remote (`transport: msgpack`) — proving the async loop config-remote
# path, NOT the `--transport msgpack client` CLI path. The node runs as a yhttp
# bridge; a /_rpc call for calculator.calc.* resolves the configured remote and
# forwards it over the async MessagePack transport to the Python server.
#
# Isolated + self-reaped: only the node process this script started is signalled
# (kill -TERM of that top-level PID — the sanctioned shutdown), nothing by name.
#
# Run from the repo root after `make build-desktop-release`.
set -uo pipefail

export YTRACE_DEFAULT_ON="${YTRACE_DEFAULT_ON:-yes}"
export YTRACE_LOG_LEVEL="${YTRACE_LOG_LEVEL:-error}"
export PICOMESH_TELEMETRY=off   # no span ship-out; keep the node free of other remotes

PICOMESH=./build-desktop-release/picomesh
SERVER=./tools/msgpack-client/calculator_server_impl.py
HOST=127.0.0.1
FPORT=7916   # foreign python msgpack service
GPORT=7917   # picomesh bridge node (yhttp /_rpc)
CFG=tmp/msgpack-remote.yaml

mkdir -p tmp
PASS=0; FAIL=0
pass() { PASS=$((PASS+1)); echo "  PASS: $1"; }
fail() { FAIL=$((FAIL+1)); echo "  FAIL: $1  --  $2" >&2; }
# expect <label> <grep-ERE> <actual>
expect() { if printf '%s' "$3" | grep -qE "$2"; then pass "$1"; else fail "$1" "$3"; fi; }

cat > "$CFG" <<EOF
mesh:
  services:
    calc_caller:
      config:
        remotes:
          - service: calculator
            host: $HOST
            port: $FPORT
            transport: msgpack
EOF

SRV= ; NODE=
cleanup() {
    [ -n "$NODE" ] && kill -TERM "$NODE" 2>/dev/null || true
    [ -n "$SRV" ]  && kill -TERM "$SRV"  2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo "[1/3] foreign python calculator (msgpack) on :$FPORT"
timeout 30 "$SERVER" "$HOST" "$FPORT" > tmp/mp-remote-foreign.log 2>&1 &
SRV=$!
up=0
for _ in $(seq 1 40); do
    if (exec 3<>"/dev/tcp/$HOST/$FPORT") 2>/dev/null; then exec 3>&- 3<&-; up=1; break; fi
    sleep 0.25
done
[ "$up" = 1 ] && pass "foreign server up on :$FPORT" || fail "foreign server never came up" "tmp/mp-remote-foreign.log"

# calculator is loaded locally ONLY for its class interface (method signatures);
# because it is ALSO a configured remote, engine_service_ctx hands back the
# remote peer first, so the call routes over the msgpack transport, not locally.
echo "[2/3] picomesh node: calculator as transport:msgpack remote, yhttp /_rpc on :$GPORT"
timeout 30 "$PICOMESH" --name calc_caller --config-file "$CFG" --plugins calculator \
    --frontend yhttp --host "$HOST" --port "$GPORT" serve > tmp/mp-remote-node.log 2>&1 &
NODE=$!
bound=0
for _ in $(seq 1 60); do
    if ss -ltn 2>/dev/null | grep -qE ":$GPORT\b"; then bound=1; break; fi
    sleep 0.25
done
[ "$bound" = 1 ] && pass "bridge bound on :$GPORT" || fail "bridge never bound" "tmp/mp-remote-node.log"

echo "[3/3] invoke calculator.calc.* THROUGH the config remote"
out=$(curl -s -XPOST "http://$HOST:$GPORT/_rpc" -H 'content-type: application/json' \
      -d '{"path":"calculator.calc.add","args":[2,3]}' 2>&1)
echo "  add -> $out"
expect "C node -> foreign msgpack remote: add(2,3) == 5" '"result":[[:space:]]*5' "$out"

out=$(curl -s -XPOST "http://$HOST:$GPORT/_rpc" -H 'content-type: application/json' \
      -d '{"path":"calculator.calc.mul","args":[6,7]}' 2>&1)
echo "  mul -> $out"
expect "C node -> foreign msgpack remote: mul(6,7) == 42" '"result":[[:space:]]*42' "$out"

echo
echo "========================================"
echo "PASS: $PASS    FAIL: $FAIL"
echo "========================================"
[ "$FAIL" -eq 0 ]
