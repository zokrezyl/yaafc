"""Issue #30 — namespace-based RBAC acceptance tests.

These are the acceptance cases the issue requires, exercised end-to-end through
the GATEWAY (never a backend port), exactly as a real client would:

  * a personal-namespace owner can manage their own repo;
  * a group developer can push to a group repo;
  * a group reporter can read but NOT push;
  * an inherited parent-namespace role applies to a subgroup repo;
  * a user with no namespace role is denied;
  * the site-admin bypass works (and only for the site owner).

Plus the bypass-class regressions found hardening the model: slug-injection,
root-group creation (GitLab-style: allowed by default, restrictable by config;
`site` always reserved), canonical-authority
(grant/repo on a nonexistent namespace is rejected), public-vs-private reads,
cross-namespace listing, runner-only lease, and namespace-gated pipeline/issue
writes + repo delete.

The module owns an ISOLATED mesh (its own data root + dynamically-allocated
ports) so it is independent of the session stack and of test ordering — the
first account it registers deterministically becomes the deployment site owner.
Teardown is the mesh-sanctioned one: SIGTERM the control parent we started and
let its reaper take the spawned children down (never pkill, never a backend
port).
"""

import collections
import contextlib
import http.cookiejar
import json
import os
import socket
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request

import pytest

Mesh = collections.namedtuple("Mesh", "gateway bridge")

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
PICOMESH = os.path.join(REPO_ROOT, "build-desktop-release", "picomesh")
CONFIG = os.path.join(REPO_ROOT, "assets", "picoforge", "config", "picoforge.yaml")
ROOT = "/tmp/picoforge-itest-rbac"


def _grab(host, results):
    sock = socket.socket()
    try:
        sock.bind((host, 0))
        port = sock.getsockname()[1]
        results.append(sock)  # hold it bound so the same port isn't picked twice
        return port
    except OSError:
        sock.close()
        raise


def _free_block(size, held, lo=18300, hi=19999):
    """Find a CONTIGUOUS run of `size` free loopback ports for portalloc's
    dynamic range; hold them bound (in `held`) until release. Returns the start
    port. Raises if no block is free in [lo, hi)."""
    for start in range(lo, hi - size):
        socks = []
        ok = True
        for port in range(start, start + size):
            sock = socket.socket()
            try:
                sock.bind(("127.0.0.1", port))
                socks.append(sock)
            except OSError:
                ok = False
                sock.close()
                break
        if ok:
            held.extend(socks)
            return start
        for sock in socks:
            sock.close()
    raise RuntimeError("no free portalloc block")


def _wait_port(port, timeout, host="127.0.0.1"):
    end = time.time() + timeout
    while time.time() < end:
        with socket.socket() as sock:
            sock.settimeout(0.3)
            try:
                sock.connect((host, port))
                return True
            except OSError:
                time.sleep(0.2)
    return False


@contextlib.contextmanager
def _spawn_mesh(root, extra_config=()):
    """Bring up an isolated picoforge mesh under `root` with optional extra
    `--config key=value` overrides; yield a Mesh(gateway, bridge)."""
    if not os.path.exists(PICOMESH):
        pytest.skip("picomesh missing — run `make build-desktop-release` first")

    held = []
    ctrl, reg, web, bridge, console = (_grab("127.0.0.1", held) for _ in range(5))
    # portalloc needs a CONTIGUOUS block of free ports for the ~18 auto backends.
    pa_lo = _free_block(60, held)
    pa_hi = pa_lo + 60
    for sock in held:  # free everything so the mesh can bind for real
        sock.close()

    subprocess.run(["rm", "-rf", root])
    os.makedirs(root, exist_ok=True)
    env = dict(os.environ)
    env.setdefault("PICOMESH_JWT_SECRET", "picoforge-itest-rbac-secret")

    argv = [PICOMESH, "--config-file", CONFIG, "--frontend", "yhttp",
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
            "--config", f"mesh.services.portalloc.config.portalloc.port_range={pa_lo}-{pa_hi}"]
    for kv in extra_config:
        argv += ["--config", kv]
    argv.append("serve")

    parent = subprocess.Popen(argv, env=env,
                              stdout=open(f"{root}/parent.log", "w"),
                              stderr=subprocess.STDOUT)
    try:
        assert _wait_port(ctrl, 15), "control parent did not bind"
        handle = _ctrl_post(ctrl, "/create", {"class": "mesh_mesh"})["handle"]
        _ctrl_post(ctrl, "/invoke",
                   {"method": "mesh_mesh_reconcile_from_config", "handle": handle, "args": []})
        assert _wait_port(web, 30), "gateway did not bind"
        time.sleep(1.5)  # let backends finish binding
        yield Mesh(f"http://127.0.0.1:{web}", f"http://127.0.0.1:{bridge}")
    finally:
        parent.terminate()
        try:
            parent.wait(timeout=10)
        except subprocess.TimeoutExpired:
            parent.kill()


@pytest.fixture(scope="module")
def mesh():
    """An isolated picoforge mesh; yields a Mesh(gateway, bridge) of base URLs."""
    with _spawn_mesh(ROOT) as m:
        yield m


@pytest.fixture(scope="module")
def restricted_mesh():
    """A second isolated mesh that DISALLOWS user root-group creation
    (accounts.allow_user_root_groups=false)."""
    with _spawn_mesh(ROOT + "-restricted",
                     ["mesh.services.accounts.config.accounts.allow_user_root_groups=false"]) as m:
        yield m


@pytest.fixture(scope="module")
def gw(mesh):
    return mesh.gateway


def _bridge_rpc(bridge, path, args):
    """POST /_rpc on the loopback operator bridge (no auth, fronts every
    backend). Returns (status_code, parsed_json)."""
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


def _ctrl_post(ctrl, path, obj):
    data = json.dumps(obj).encode()
    req = urllib.request.Request(f"http://127.0.0.1:{ctrl}{path}", data=data,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=10) as resp:
        return json.loads(resp.read())


class Client:
    """A gateway client holding one user's session cookie."""

    def __init__(self, base):
        self.base = base
        self.jar = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(
            urllib.request.HTTPCookieProcessor(self.jar),
            _NoRedirect())

    def register(self, user, password):
        self._form("/register", user, password)
        return self

    def login(self, user, password):
        self._form("/login", user, password)
        return self

    def _form(self, path, user, password):
        body = urllib.parse.urlencode({"username": user, "password": password}).encode()
        req = urllib.request.Request(self.base + path, data=body, method="POST")
        try:
            self.opener.open(req, timeout=10).read()
        except urllib.error.HTTPError as exc:
            exc.read()  # 303 lands here via _NoRedirect; cookies are still captured

    def rpc(self, path, args):
        """(status_code, parsed_json_or_text)."""
        body = json.dumps({"path": path, "args": args}).encode()
        req = urllib.request.Request(self.base + "/_rpc", data=body, method="POST",
                                     headers={"Content-Type": "application/json"})
        try:
            resp = self.opener.open(req, timeout=15)
            return resp.status, json.loads(resp.read())
        except urllib.error.HTTPError as exc:
            raw = exc.read().decode()
            try:
                return exc.code, json.loads(raw)
            except json.JSONDecodeError:
                return exc.code, raw

    def result(self, path, args):
        status, body = self.rpc(path, args)
        assert status == 200, f"{path} -> {status}: {body}"
        return body["result"]

    def code(self, path, args):
        return self.rpc(path, args)[0]

    @property
    def uid(self):
        req = urllib.request.Request(self.base + "/_whoami")
        return json.loads(self.opener.open(req, timeout=10).read())["uid"]


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *_args, **_kwargs):
        return None  # don't follow 303s; we only need the Set-Cookie


@pytest.fixture(scope="module")
def actors(gw):
    """Register the deployment: the FIRST account (`root`) becomes site owner,
    then alice (personal-namespace owner), bob (→developer), erin (→reporter),
    dave (no role). A group `acme` with a subgroup `acme/platform` and two repos
    is provisioned by root."""
    root = Client(gw).register("root", "rootpw")
    alice = Client(gw).register("alice", "alicepw")
    bob = Client(gw).register("bob", "bobpw")
    erin = Client(gw).register("erin", "erinpw")
    dave = Client(gw).register("dave", "davepw")

    # root (site owner) builds the group, members, subgroup and two repos.
    assert root.result("accounts.accounts.ns_create", [root.uid, "group", "acme", ""]) == "acme"
    assert root.result("accounts.accounts.ns_add_member", ["acme", bob.uid, "developer"]) == 1
    assert root.result("accounts.accounts.ns_add_member", ["acme", erin.uid, "reporter"]) == 1
    assert root.result("accounts.accounts.ns_create", [root.uid, "group", "platform", "acme"]) == "acme/platform"
    api = root.result("git_repo.git_repo.make", [root.uid, "acme", "api"])
    svc = root.result("git_repo.git_repo.make", [root.uid, "acme/platform", "svc"])

    # Roles are point-in-time JWT claims minted at login → re-login bob/erin so
    # their tokens carry the memberships granted above.
    bob.login("bob", "bobpw")
    erin.login("erin", "erinpw")
    return dict(root=root, alice=alice, bob=bob, erin=erin, dave=dave, api=api, svc=svc)


# ---- the issue #30 acceptance cases -------------------------------------------

def test_personal_namespace_owner_manages_own_repo(actors):
    alice = actors["alice"]
    rid = alice.result("git_repo.git_repo.make", [alice.uid, "alice", "site"])
    oid = alice.result("git_repo.git_repo.put_file",
                       [rid, "README.md", "hi", "init", "alice", "a@x"])
    assert isinstance(oid, str) and oid  # a commit hash


def test_group_developer_can_push(actors):
    oid = actors["bob"].result("git_repo.git_repo.put_file",
                               [actors["api"], "a.txt", "x", "c", "bob", "b@x"])
    assert isinstance(oid, str) and oid


def test_group_reporter_can_read_but_not_push(actors):
    erin = actors["erin"]
    assert erin.code("git_repo.git_repo.read_tree", [actors["api"], "", ""]) == 200
    assert erin.code("git_repo.git_repo.put_file",
                     [actors["api"], "z.txt", "x", "c", "erin", "e@x"]) == 403


def test_inherited_parent_role_applies_to_subgroup_repo(actors):
    # bob is developer on `acme`; that inherits down to `acme/platform/svc`.
    oid = actors["bob"].result("git_repo.git_repo.put_file",
                               [actors["svc"], "s.txt", "x", "c", "bob", "b@x"])
    assert isinstance(oid, str) and oid


def test_user_with_no_namespace_role_is_denied(actors):
    assert actors["dave"].code("git_repo.git_repo.put_file",
                               [actors["api"], "d.txt", "x", "c", "dave", "d@x"]) == 403


def test_site_owner_bypass_pushes_any_repo(actors):
    oid = actors["root"].result("git_repo.git_repo.put_file",
                                [actors["api"], "r.txt", "x", "c", "root", "r@x"])
    assert isinstance(oid, str) and oid


# ---- bypass-class regressions the hardening introduced ------------------------

def test_non_member_cannot_grant_roles(actors):
    assert actors["dave"].code("accounts.accounts.ns_add_member",
                               ["acme", actors["dave"].uid, "owner"]) == 403


def test_user_can_create_root_group_by_default(actors):
    """GitLab-style: with the default config (accounts.allow_user_root_groups
    true) any authenticated user may create a top-level group and owns it. (The
    owner membership lands immediately; it appears in the user's JWT claims on
    next login, the usual point-in-time-claims rule — so we assert creation, the
    feature, not an immediate same-token subgroup write.)"""
    bob = actors["bob"]
    assert bob.result("accounts.accounts.ns_create",
                      [bob.uid, "group", "bobgroup", ""]) == "bobgroup"
    # Re-login to pick up bobgroup:owner, then the subgroup write is permitted.
    bob.login("bob", "bobpw")
    assert bob.result("accounts.accounts.ns_create",
                      [bob.uid, "group", "sub", "bobgroup"]) == "bobgroup/sub"


def test_reserved_site_namespace_is_never_user_creatable(actors):
    """`site` stays reserved to the internal bootstrap regardless of the
    allow-root-groups setting."""
    assert actors["bob"].code("accounts.accounts.ns_create",
                              [actors["bob"].uid, "group", "site", ""]) == 500


def test_slug_injection_rejected(actors):
    # A slug with a comma/colon must not smuggle a second "<path>:<role>".
    assert actors["root"].code("accounts.accounts.ns_create",
                               [actors["root"].uid, "group", "evil,site", ""]) == 500


def test_grant_on_nonexistent_namespace_rejected(actors):
    assert actors["root"].code("accounts.accounts.ns_add_member",
                               ["ghost", actors["bob"].uid, "developer"]) == 500


def test_repo_under_nonexistent_namespace_rejected(actors):
    assert actors["root"].code("git_repo.git_repo.make",
                               [actors["root"].uid, "ghostns", "r"]) == 500


def test_public_repo_is_anonymously_readable(actors, gw):
    alice = actors["alice"]
    rid = alice.result("git_repo.git_repo.make", [alice.uid, "alice", "public-demo"])
    alice.result("git_repo.git_repo.set_public", [rid, 1])
    anon = Client(gw)  # no session
    assert anon.code("git_repo.git_repo.read_tree", [rid, "", ""]) == 200
    # a private repo refuses the same anonymous read
    assert anon.code("git_repo.git_repo.read_tree", [actors["api"], "", ""]) == 401


def test_cross_namespace_repo_listing_denied(actors):
    alice, dave = actors["alice"], actors["dave"]
    assert alice.code("git_repo.git_repo.list_for_owner", [alice.uid]) == 200
    assert dave.code("git_repo.git_repo.list_for_owner", [alice.uid]) == 500


def test_pipeline_writes_are_namespace_gated(actors):
    # developer (bob) may enqueue on the group repo; a no-role user may not.
    assert actors["bob"].code("git_pipeline.git_pipeline.enqueue_job",
                              [actors["api"], "refs/heads/main", "", 60]) == 200
    assert actors["dave"].code("git_pipeline.git_pipeline.enqueue_job",
                               [actors["api"], "refs/heads/x", "", 60]) == 403


def test_legacy_lease_and_complete_are_not_user_callable(actors):
    # lease is runner-only (policy group); complete is absent from policy.
    assert actors["alice"].code("git_pipeline.git_pipeline.lease", [1]) == 403
    assert actors["dave"].code("git_pipeline.git_pipeline.complete", [1, 0]) == 403


def test_repo_delete_is_maintainer_gated(actors):
    alice, dave = actors["alice"], actors["dave"]
    rid = alice.result("git_repo.git_repo.make", [alice.uid, "alice", "throwaway"])
    assert dave.code("git_repo.git_repo.delete", [rid]) == 403
    assert alice.result("git_repo.git_repo.delete", [rid]) == 1


def test_mint_cannot_forge_a_privileged_jwt(mesh):
    """The token issuer's low-level mint is fronted UNAUTHENTICATED by the
    loopback operator bridge. Because the RBAC model trusts signed JWT groups,
    mint must refuse any privilege-granting claim — otherwise a caller reaching
    the bridge could mint itself a `system:internal` / `site:owner` / namespace-
    owner JWT and bypass authorization. Non-privileged runner tokens stay
    mintable (their legitimate use)."""
    for forged in ("system:internal", "site:owner", "site:maintainer", "acme:owner"):
        status, body = _bridge_rpc(
            mesh.bridge, "token_issuer.token_issuer.mint", [9999, "x", forged, 0])
        assert status != 200, f"mint forged a privileged token for {forged!r}: {body}"

    status, body = _bridge_rpc(
        mesh.bridge, "token_issuer.token_issuer.mint",
        [9999, "runner9999", "site:runner,runner:9999", 0])
    assert status == 200 and isinstance(body.get("result"), str), \
        f"mint should still issue a non-privileged runner token: {body}"


def test_ns_members_requires_maintainer_and_carries_usernames(actors):
    """ns_members is fail-closed (maintainer+ on the namespace), and the rows
    carry the username (joined server-side) so the UI needs no admin roster."""
    assert actors["dave"].code("accounts.accounts.ns_members", ["acme"]) != 200
    status, body = actors["root"].rpc("accounts.accounts.ns_members", ["acme"])
    assert status == 200, body
    members = body["result"]
    if isinstance(members, str):
        members = json.loads(members)
    assert members and any(m.get("username") for m in members), members


def test_ns_list_is_site_admin_only(actors):
    assert actors["dave"].code("accounts.accounts.ns_list", []) != 200
    assert actors["root"].code("accounts.accounts.ns_list", []) == 200


def test_reserved_repo_name_rejected(actors):
    # A repo named after a route word would be unbrowseable in a nested namespace.
    assert actors["root"].code("git_repo.git_repo.make",
                               [actors["root"].uid, "acme", "settings"]) == 500


def test_issue_count_is_namespace_gated(actors):
    # a no-role user cannot read a private repo's open-issue count
    assert actors["dave"].code("issues.issues.count_open_in_repo", [actors["api"]]) != 200


def test_global_list_scans_are_fail_closed_on_the_bridge(actors, mesh):
    """The unbounded cross-repo scans (issues/pipeline list_all) are operator
    surfaces fronted UNAUTHENTICATED by the loopback bridge — they must reject a
    credential-less caller (site-admin only), not dump every row. (`actors`
    forces the mesh to be provisioned first.)"""
    for path in ("issues.issues.list_all", "git_pipeline.git_pipeline.list_all"):
        status, _ = _bridge_rpc(mesh.bridge, path, [])
        assert status != 200, f"{path} returned rows to an unauthenticated bridge caller"


def test_inherited_role_allows_subgroup_repo_creation(actors):
    """A developer on acme can create a repo in the subgroup acme/platform via
    inherited role (the namespace need not be a direct membership)."""
    rid = actors["bob"].result("git_repo.git_repo.make",
                               [actors["bob"].uid, "acme/platform", "bob-inherited"])
    assert isinstance(rid, int) and rid > 0


def test_first_user_is_the_site_owner(actors):
    """The first registrant (`root`) owns the `site` namespace and so wields the
    site-admin bypass; a later regular user (alice) does not. Probe with a
    site-admin-only operation (ns_list — enumerating the whole namespace tree)."""
    assert actors["root"].code("accounts.accounts.ns_list", []) == 200
    assert actors["alice"].code("accounts.accounts.ns_list", []) != 200


def test_overlong_namespace_path_is_rejected_not_truncated(actors):
    """A namespace path that would not fit git_repo's owner_name field must be
    rejected at validation, never silently truncated (which would desync the
    deterministic repo id from the stored path)."""
    root = actors["root"]
    long_seg = "x" * 70  # > 63: each path segment / whole path must fit 64-byte field
    # accounts refuses to even create such a namespace ...
    assert root.code("accounts.accounts.ns_create",
                     [root.uid, "group", long_seg, ""]) == 500
    # ... and git_repo.make refuses an overlong owner_name outright (path_ok).
    assert root.code("git_repo.git_repo.make", [root.uid, long_seg, "r"]) == 500


def test_make_cannot_forge_creator_uid(actors):
    """git_repo.make takes the creator uid from verified auth, not the RPC arg:
    a developer on `acme` who passes another user's uid cannot poison that
    user's creator index — the repo is recorded under the real caller."""
    bob, alice = actors["bob"], actors["alice"]
    rid = bob.result("git_repo.git_repo.make", [alice.uid, "acme", "forged"])
    assert isinstance(rid, int) and rid > 0
    bob_repos = bob.result("git_repo.git_repo.list_for_owner", [bob.uid])
    alice_repos = alice.result("git_repo.git_repo.list_for_owner", [alice.uid])
    assert "acme/forged" in bob_repos      # recorded under the real creator
    assert "acme/forged" not in alice_repos  # NOT under the impersonated uid


def test_group_repo_is_indexed_under_namespace_for_rbac_discovery(actors):
    """A repo created in a group namespace is discoverable by namespace (the
    webapp /repos page enumerates this for every member, regardless of creator):
    root created acme/api, and a reporter (erin) can enumerate it by namespace."""
    names = actors["erin"].result("git_repo.git_repo.list_for_namespace", ["acme"])
    assert "api" in names


def test_ns_subtree_returns_descendants_for_inherited_discovery(actors):
    """ns_subtree returns a namespace AND its descendants, so a developer on
    `acme` discovers `acme/platform` (and its repos) by INHERITED role — this is
    what makes the Projects page list inherited subgroup repos the user never had
    a direct grant on and did not create."""
    rows = actors["bob"].result("accounts.accounts.ns_subtree", ["acme"])
    paths = {row["path"] for row in rows}
    assert "acme" in paths
    assert "acme/platform" in paths


def test_ns_subtree_is_fail_closed_for_non_members(actors):
    """A user with no role on the namespace cannot enumerate its subtree."""
    assert actors["dave"].code("accounts.accounts.ns_subtree", ["acme"]) != 200


def test_ns_delete_is_not_exposed_at_the_gateway(actors):
    """ns_delete is internal-only (the first-user site bootstrap rollback calls
    it over a direct service connection). It has no gateway policy entry, so even
    a site admin is denied via /_rpc — the namespace tree can't be pruned from
    outside the mesh."""
    assert actors["root"].code("accounts.accounts.ns_delete", ["acme"]) != 200


def test_ns_subtree_does_not_leak_siblings_via_sql_wildcard(actors):
    """Slugs allow `_`, which is a SQLite LIKE single-char wildcard. ns_subtree
    must use an EXACT prefix match, so ns_subtree("a_b") must NOT disclose a
    sibling namespace `axb/...` that a naive LIKE 'a_b/%' would match."""
    root = actors["root"]
    assert root.result("accounts.accounts.ns_create", [root.uid, "group", "a_b", ""]) == "a_b"
    assert root.result("accounts.accounts.ns_create", [root.uid, "group", "axb", ""]) == "axb"
    assert root.result("accounts.accounts.ns_create", [root.uid, "group", "child", "axb"]) == "axb/child"
    paths = {row["path"] for row in root.result("accounts.accounts.ns_subtree", ["a_b"])}
    assert "a_b" in paths
    assert not any(p == "axb" or p.startswith("axb/") for p in paths), paths


def test_revoked_member_loses_subtree_discovery(actors, gw):
    """Discovery is role-based, not creator-based, so it follows REVOCATION:
    after ns_remove_member the user can no longer enumerate the namespace subtree,
    which is exactly why the Projects page stops listing that namespace's repos
    (the creator-index fallback that used to leak them is gone)."""
    frank = Client(gw).register("frank", "frankpw")
    actors["root"].result("accounts.accounts.ns_add_member", ["acme", frank.uid, "reporter"])
    frank.login("frank", "frankpw")  # JWT claims are point-in-time → re-login for the grant
    assert frank.code("accounts.accounts.ns_subtree", ["acme"]) == 200
    actors["root"].result("accounts.accounts.ns_remove_member", ["acme", frank.uid])
    frank.login("frank", "frankpw")  # re-login to pick up the revocation
    assert frank.code("accounts.accounts.ns_subtree", ["acme"]) != 200


def test_root_group_creation_can_be_restricted_by_config(restricted_mesh):
    """With accounts.allow_user_root_groups=false, a regular user is denied a
    root group, but the FIRST registrant (site owner) still may, and a personal
    namespace is still bootstrapped at register (the config only gates
    non-admin, non-system root creation)."""
    gw = restricted_mesh.gateway
    owner = Client(gw).register("owner", "ownerpw")          # first user → site owner
    user = Client(gw).register("user", "userpw")             # regular user
    # The regular user cannot create a root group under the restrictive config.
    assert user.code("accounts.accounts.ns_create", [user.uid, "group", "ug", ""]) == 500
    # The site owner still can.
    assert owner.result("accounts.accounts.ns_create", [owner.uid, "group", "og", ""]) == "og"
