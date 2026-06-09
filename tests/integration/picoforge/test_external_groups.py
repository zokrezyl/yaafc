"""Issue #31 — pluggable external-directory group resolution.

`accounts.accounts.groups(uid)` is the single facade the mesh consumes for a
user's flattened "<namespace>:<role>,…" claims. With `accounts.external_groups`
configured it merges the local namespace_members grants with roles mapped from
an external directory provider, highest-role-wins per namespace. This test drives
that end to end against an isolated mesh, asserting:

  * config-backed provider maps an external group to namespace:role and merges
    it with the user's local grants;
  * higher role wins when two external groups map the same namespace;
  * the LDAP/AD-style provider (mockable adapter) resolves through mock_members;
  * a provider error (ldap with no reachable directory) FAILS CLOSED.

The merged CSV is read directly from `accounts.groups(uid)` over the loopback
operator bridge (unauthenticated, fronts every backend) — the same value
token_issuer folds into the JWT. Local-only behavior (no provider configured) is
already covered by the full mesh-up smoke (external_groups defaults to disabled).
"""
import http.cookiejar
import json
import os
import socket
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request
from contextlib import contextmanager

import pytest

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
PICOMESH = os.path.join(REPO_ROOT, "build-desktop-release", "picomesh")
CONFIG = os.path.join(REPO_ROOT, "assets", "picoforge", "config", "picoforge.yaml")
ROOT = "/tmp/picoforge-itest-extgroups"

# The exact anchor in picoforge.yaml we replace to inject a test provider config.
_ANCHOR = ("          external_groups:\n"
           "            enabled: false\n"
           "            fail_closed: true\n"
           "            provider: config")


def _grab(host, held):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((host, 0))
    held.append(sock)
    return sock.getsockname()[1]


def _free_block(size, held, lo=18200, hi=19999):
    for base in range(lo, hi - size):
        socks = []
        try:
            for off in range(size):
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                s.bind(("127.0.0.1", base + off))
                socks.append(s)
            for s in socks:
                s.close()
            return base
        except OSError:
            for s in socks:
                s.close()
    raise RuntimeError("no free port block")


def _wait_port(port, timeout, host="127.0.0.1"):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=1):
                return True
        except OSError:
            time.sleep(0.2)
    return False


def _ctrl_post(ctrl, path, obj):
    data = json.dumps(obj).encode()
    req = urllib.request.Request(f"http://127.0.0.1:{ctrl}{path}", data=data,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=10) as resp:
        return json.loads(resp.read())


def _bridge_rpc(bridge, path, args):
    body = json.dumps({"path": path, "args": args}).encode()
    req = urllib.request.Request(bridge + "/_rpc", data=body, method="POST",
                                 headers={"Content-Type": "application/json"})
    try:
        resp = urllib.request.urlopen(req, timeout=15)
        return resp.status, json.loads(resp.read())
    except urllib.error.HTTPError as exc:
        raw = exc.read().decode()
        try:
            return exc.code, json.loads(raw)
        except json.JSONDecodeError:
            return exc.code, {"raw": raw}


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *a, **k):
        return None


def _register(base, user, password):
    jar = http.cookiejar.CookieJar()
    opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar), _NoRedirect())
    body = urllib.parse.urlencode({"username": user, "password": password}).encode()
    req = urllib.request.Request(base + "/register", data=body, method="POST")
    try:
        opener.open(req, timeout=10).read()
    except urllib.error.HTTPError as exc:
        exc.read()  # 303 / error page both land here; the account row is what matters


@contextmanager
def _mesh(root, external_groups_yaml):
    """Bring up an isolated mesh whose accounts.external_groups block is replaced
    with `external_groups_yaml` (already indented to 10 spaces under it)."""
    if not os.path.exists(PICOMESH):
        pytest.skip("picomesh missing — run `make build-desktop-release` first")
    with open(CONFIG) as handle:
        config_text = handle.read()
    assert _ANCHOR in config_text, "external_groups anchor not found — config drifted"
    config_text = config_text.replace(_ANCHOR, external_groups_yaml, 1)

    held = []
    ctrl, reg, web, bridge, console = (_grab("127.0.0.1", held) for _ in range(5))
    pa_lo = _free_block(60, held)
    for sock in held:
        sock.close()

    subprocess.run(["rm", "-rf", root])
    os.makedirs(root, exist_ok=True)
    config_path = os.path.join(root, "config.yaml")
    with open(config_path, "w") as handle:
        handle.write(config_text)

    env = dict(os.environ)
    env.setdefault("PICOMESH_JWT_SECRET", "picoforge-itest-extgroups-secret")
    argv = [PICOMESH, "--config-file", config_path, "--frontend", "yhttp",
            "--host", "127.0.0.1", "--port", str(ctrl),
            "--config", f"mesh.services.registry.port={reg}",
            "--config", f"mesh.services.gateway.port={web}",
            "--config", f"storage.db_path={root}/mesh-state.db",
            "--config", f"mesh.nodes_dir={root}/nodes",
            "--config", f"mesh.services.sharded_storage.config.sharded_storage.path={root}/sharded",
            "--config", f"mesh.services.rstore_uid.config.relational_storage.path={root}/rel/uid",
            "--config", f"mesh.services.rstore_username.config.relational_storage.path={root}/rel/username",
            "--config", f"mesh.services.rstore_session.config.relational_storage.path={root}/rel/session",
            "--config", f"mesh.services.rstore_token.config.relational_storage.path={root}/rel/token",
            "--config", f"mesh.services.git_repo.config.git_repo.repos_dir={root}/repos",
            "--config", f"mesh.services.internal_yhttp_bridge.port={bridge}",
            "--config", f"mesh.services.service_console.port={console}",
            "--config", f"mesh.services.service_console.config.alpine.upstream.port={bridge}",
            "--config", f"mesh.services.portalloc.config.portalloc.port_range={pa_lo}-{pa_lo + 60}",
            "serve"]
    parent = subprocess.Popen(argv, env=env,
                              stdout=open(f"{root}/parent.log", "w"),
                              stderr=subprocess.STDOUT)
    try:
        assert _wait_port(ctrl, 15), "control parent did not bind"
        handle = _ctrl_post(ctrl, "/create", {"class": "mesh_mesh"})["handle"]
        _ctrl_post(ctrl, "/invoke",
                   {"method": "mesh_mesh_reconcile_from_config", "handle": handle, "args": []})
        assert _wait_port(web, 30), "gateway did not bind"
        time.sleep(1.5)
        yield (f"http://127.0.0.1:{web}", f"http://127.0.0.1:{bridge}")
    finally:
        parent.terminate()
        try:
            parent.wait(timeout=10)
        except subprocess.TimeoutExpired:
            parent.kill()


def _uid_and_groups(gateway, bridge, user):
    """Register `user`, then read their assigned uid and merged groups CSV over
    the bridge. Returns (uid, status, groups_csv_or_body)."""
    _register(gateway, user, "hunter2")
    status, body = _bridge_rpc(bridge, "accounts.accounts.uid_for_username", [user])
    assert status == 200, f"uid_for_username -> {status}: {body}"
    uid = body["result"]
    assert uid and uid > 0, f"no uid for {user}: {body}"
    status, body = _bridge_rpc(bridge, "accounts.accounts.groups", [uid])
    return uid, status, body


def test_config_provider_maps_merges_and_highest_role_wins():
    """config-backed provider: an external group maps to namespace:role, merges
    with the user's local personal-namespace grant, and the highest role wins
    when two external groups target the same namespace."""
    cfg = (
        "          external_groups:\n"
        "            enabled: true\n"
        "            fail_closed: true\n"
        "            provider: config\n"
        "            members:\n"
        "              alice: [acme-devs, acme-leads, picoforge-admins]\n"
        "            mappings:\n"
        "              - { external_group: acme-devs,        namespace: acme/platform, role: developer }\n"
        "              - { external_group: acme-leads,       namespace: acme/platform, role: maintainer }\n"
        "              - { external_group: picoforge-admins, namespace: site,          role: reporter }\n"
    )
    with _mesh(ROOT + "-config", cfg) as (gateway, bridge):
        # The FIRST registrant becomes the deployment site:owner (bootstrap), so
        # register a founder first; alice is then a regular second user with no
        # local `site` grant, isolating the external site mapping.
        _register(gateway, "founder", "hunter2")
        uid, status, body = _uid_and_groups(gateway, bridge, "alice")
        assert status == 200, f"groups -> {status}: {body}"
        groups = body["result"]
        roles = set(groups.split(",")) if groups else set()
        # local grant: alice owns her personal namespace.
        assert "alice:owner" in roles, roles
        # external mapping merged in; highest role wins for acme/platform.
        assert "acme/platform:maintainer" in roles, roles
        assert "acme/platform:developer" not in roles, roles
        # external-only namespace (alice has no local site grant) maps through.
        assert "site:reporter" in roles, roles


def test_ldap_provider_resolves_through_mock_members():
    """The LDAP/AD-style adapter resolves membership through its mock_members
    block (the dev/test stand-in for a real bind+search)."""
    cfg = (
        "          external_groups:\n"
        "            enabled: true\n"
        "            fail_closed: true\n"
        "            provider: ldap\n"
        "            mock_members:\n"
        "              bob: [acme-devs]\n"
        "            mappings:\n"
        "              - { external_group: acme-devs, namespace: acme/platform, role: developer }\n"
    )
    with _mesh(ROOT + "-ldap", cfg) as (gateway, bridge):
        uid, status, body = _uid_and_groups(gateway, bridge, "bob")
        assert status == 200, f"groups -> {status}: {body}"
        roles = set(body["result"].split(",")) if body["result"] else set()
        assert "acme/platform:developer" in roles, roles


def test_ldap_provider_fails_closed_without_directory():
    """provider: ldap with no reachable directory (no mock_members) is a provider
    ERROR; with fail_closed it must fail groups(), never silently drop the
    privileged mapping."""
    cfg = (
        "          external_groups:\n"
        "            enabled: true\n"
        "            fail_closed: true\n"
        "            provider: ldap\n"
        "            mappings:\n"
        "              - { external_group: acme-devs, namespace: acme/platform, role: developer }\n"
    )
    with _mesh(ROOT + "-failclosed", cfg) as (gateway, bridge):
        _register(gateway, "carol", "hunter2")
        status, body = _bridge_rpc(bridge, "accounts.accounts.uid_for_username", ["carol"])
        # carol's account row is written before the (failing) session mint, so
        # her uid resolves even though login could not complete.
        assert status == 200 and body["result"] > 0, body
        uid = body["result"]
        status, body = _bridge_rpc(bridge, "accounts.accounts.groups", [uid])
        assert status != 200, f"groups should fail closed, got {status}: {body}"
