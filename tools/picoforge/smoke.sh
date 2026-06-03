#!/usr/bin/env bash
# picoforge smoke: launch picomesh with the scenario config, drive every
# plugin through one round-trip via the yttp JSON-RPC frontend.
#
# Expected to be run from the repo root after `make build-desktop-release`.

set -euo pipefail

PICOMESH=./build-desktop-release/picomesh
CONFIG=assets/picoforge/config/picoforge.yaml
PORT=8801

# issue #19: token_issuer signs access JWTs with this shared secret.
export PICOMESH_JWT_SECRET="${PICOMESH_JWT_SECRET:-picoforge-dev-mesh-secret-change-me}"

mkdir -p tmp

# Start the yttp server. The picoforge.yaml binds yttp on 0.0.0.0:8801 (see
# `mesh.services.yttp` in the scenario file).
"$PICOMESH" --config-file "$CONFIG" --frontend yttp --host 127.0.0.1 --port "$PORT" serve \
    > tmp/picoforge-srv.log 2>&1 &
SRV=$!
trap 'kill -INT $SRV 2>/dev/null; sleep 0.2; kill -KILL $SRV 2>/dev/null || true' EXIT
sleep 0.4

# rpc <method-json> — one request, frame it, print stripped response.
rpc() {
    local body=$1
    local len=${#body}
    printf 'Content-Length: %d\r\n\r\n%s' "$len" "$body" \
        | nc -q1 127.0.0.1 "$PORT" \
        | tr -d '\r' \
        | awk 'NR>2'
}

# create one instance of each class. We only need the class names that
# are wired into the linked binary; every plugin in the scenario maps
# to <plugin>_<class>.
declare -A H
for cls in portalloc_portalloc session_session accounts_accounts \
           password_authn_password_authn github_authn_github_authn token_issuer_token_issuer \
           issues_issues git_repo_git_repo git_pipeline_git_pipeline \
           personal_access_tokens_personal_access_tokens mesh_mesh; do
    echo "== create $cls =="
    out=$(rpc '{"jsonrpc":"2.0","id":1,"method":"create","params":{"class":"'"$cls"'"}}')
    echo "  $out"
    H[$cls]=$(echo "$out" | sed -E 's/.*"handle":([0-9]+).*/\1/')
done

call() {
    local method=$1 args=$2 handle=$3
    rpc '{"jsonrpc":"2.0","id":2,"method":"invoke","params":{"method":"'"$method"'","handle":'"$handle"',"args":'"$args"'}}'
}

echo
echo "== portalloc.allocate(1) → port =="
call portalloc_portalloc_allocate '[1]' "${H[portalloc_portalloc]}"

echo
echo "== accounts.register(uid=100), set_balance(100, 5000), balance(100) =="
call accounts_accounts_register     '[100, "user100"]'  "${H[accounts_accounts]}"
call accounts_accounts_set_balance  '[100, 5000]'  "${H[accounts_accounts]}"
call accounts_accounts_balance      '[100]'        "${H[accounts_accounts]}"

echo
echo "== password_authn.register(uid=100, hash=42), authenticate ok/bad =="
call password_authn_password_authn_register      '[100, 42]'  "${H[password_authn_password_authn]}"
call password_authn_password_authn_authenticate  '[100, 42]'  "${H[password_authn_password_authn]}"
call password_authn_password_authn_authenticate  '[100, 43]'  "${H[password_authn_password_authn]}"

echo
echo "== token_issuer.login(password, uid=100, user100, hash=42) → token pair, count_active =="
# login delegates to password_authn (uid 100 registered above with hash 42),
# loads groups from accounts, and mints {access_jwt, refresh_token, ...}.
call token_issuer_token_issuer_login '["password", 100, "user100", 42]' "${H[token_issuer_token_issuer]}"
call token_issuer_token_issuer_count_active '[]' "${H[token_issuer_token_issuer]}"

echo
echo "== personal_access_tokens.mint(uid=100) → pat, lookup =="
out=$(call personal_access_tokens_personal_access_tokens_mint '[100]' "${H[personal_access_tokens_personal_access_tokens]}")
echo "  mint: $out"
PAT=$(echo "$out" | sed -E 's/.*"result":([0-9]+).*/\1/')
call personal_access_tokens_personal_access_tokens_lookup "[$PAT]" "${H[personal_access_tokens_personal_access_tokens]}"

echo
echo "== git_repo.make(owner=100) → repo, count_for_owner =="
out=$(call git_repo_git_repo_make '[100]' "${H[git_repo_git_repo]}")
echo "  make: $out"
REPO=$(echo "$out" | sed -E 's/.*"result":([0-9]+).*/\1/')
call git_repo_git_repo_count_for_owner '[100]' "${H[git_repo_git_repo]}"

echo
echo "== issues.open(repo=$REPO, author=100), status, count_open_in_repo =="
out=$(call issues_issues_open "[$REPO, 100]" "${H[issues_issues]}")
ISS=$(echo "$out" | sed -E 's/.*"result":([0-9]+).*/\1/')
echo "  open: $out"
call issues_issues_status              "[$ISS]"   "${H[issues_issues]}"
call issues_issues_count_open_in_repo  "[$REPO]"  "${H[issues_issues]}"

echo
echo "== git_pipeline.enqueue(repo=$REPO), lease(runner=1), count_running =="
out=$(call git_pipeline_git_pipeline_enqueue "[$REPO]" "${H[git_pipeline_git_pipeline]}")
JOB=$(echo "$out" | sed -E 's/.*"result":([0-9]+).*/\1/')
echo "  enqueue: $out"
call git_pipeline_git_pipeline_lease         '[1]'      "${H[git_pipeline_git_pipeline]}"
call git_pipeline_git_pipeline_count_running ''         "${H[git_pipeline_git_pipeline]}"
call git_pipeline_git_pipeline_complete      "[$JOB, 0]" "${H[git_pipeline_git_pipeline]}"
call git_pipeline_git_pipeline_count_done    ''         "${H[git_pipeline_git_pipeline]}"

echo
echo "== mesh.register_service(service=10, port=8201), resolve(10), count =="
call mesh_mesh_register_service '[10, 8201]' "${H[mesh_mesh]}"
call mesh_mesh_resolve          '[10]'       "${H[mesh_mesh]}"
call mesh_mesh_count_services   ''           "${H[mesh_mesh]}"

echo
echo "== session.start(uid=100, jwt, refresh) → opaque sid, lookup =="
# issue #19: a session now stores the access JWT + refresh keyed by sid; start
# returns the opaque sid string, and lookup maps it back to the uid.
out=$(call session_session_start '[100, "smoke-jwt", "smoke-refresh"]' "${H[session_session]}")
SID=$(echo "$out" | sed -E 's/.*"result":"([0-9a-f]+)".*/\1/')
echo "  start: $out"
call session_session_lookup "[\"$SID\"]" "${H[session_session]}"

echo
echo "== github_authn.set_credentials(client=1, secret=2), register_code, resolve =="
call github_authn_github_authn_set_credentials '[1, 2]'        "${H[github_authn_github_authn]}"
call github_authn_github_authn_register_code   '[12345, 100]'  "${H[github_authn_github_authn]}"
call github_authn_github_authn_resolve         '[12345]'       "${H[github_authn_github_authn]}"

echo
echo "smoke: all backend plugins exercised end-to-end."
