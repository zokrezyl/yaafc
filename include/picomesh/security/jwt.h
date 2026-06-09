/* jwt — HS256 JSON Web Tokens for the picomesh trusted mesh.
 *
 * The gateway authenticates an external credential into a verified JWT; that
 * JWT travels to backends in yheaders. HS256 is used deliberately: the mesh
 * (gateway↔backends) is a trusted boundary that shares one signing secret, so
 * a symmetric MAC is sufficient and avoids pulling in an asymmetric crypto
 * dependency. An untrusted edge would want RS256/EdDSA instead.
 *
 * Claims carried (see docs/security.md):
 *   iss, sub (uid), username, groups ([]"<account>:<role>"), iat, exp, jti.
 *
 * Authorization roles use a monotonic ladder:
 *   guest < reporter < developer < maintainer < owner
 * Group membership strings are "<account-slug>:<role>". */

#ifndef PICOMESH_SECURITY_JWT_H
#define PICOMESH_SECURITY_JWT_H

#include <stddef.h>
#include <stdint.h>

#include <picomesh/core/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Current wall-clock time in seconds (for iat/exp). */
int64_t picomesh_security_now(void);

/* Build a compact claims JSON object. `groups_csv` is a comma-separated list
 * of "<account>:<role>" strings (may be NULL/empty); it is emitted as a JSON
 * array. Returns a malloc'd NUL-terminated string the caller frees, or NULL on
 * allocation failure. */
char *picomesh_jwt_build_claims(const char *issuer, uint32_t sub,
                                const char *username, const char *groups_csv,
                                int64_t issued_at, int64_t expires_at);

/* Sign `claims_json` with HS256 under `secret`. Returns a malloc'd JWT
 * (header.payload.signature, base64url), or NULL on failure. */
char *picomesh_jwt_encode(const char *claims_json, const char *secret);

/* Verify signature + expiry of `jwt` under `secret` at time `now` (seconds).
 * On success the OK value is the malloc'd payload-claims JSON (caller frees).
 * Fails closed on a bad signature, a non-HS256 alg header, a malformed token,
 * or an expired token. */
struct picomesh_string_result
picomesh_jwt_verify(const char *jwt, const char *secret, int64_t now);

/* The reserved INTERNAL-capability group. The gateway (which holds the signing
 * secret) mints a short-lived JWT carrying this group for its own trusted
 * bootstrap operations (creating a new user's namespace, the first-user `site`
 * namespace, the /repos/new repo). It is NEVER issued to a client and never
 * derived from a user's memberships, so a backend can treat it as the explicit
 * "trusted internal caller" capability — replacing the unsafe "no JWT means
 * trusted" assumption with a signed, verifiable marker. */
#define PICOMESH_GROUP_SYSTEM "system:internal"

/* 1 if the comma-separated `groups_csv` contains the exact membership `group`.
 */
int picomesh_groups_contains(const char *groups_csv, const char *group);

/* Role ladder: rank of a role name, or -1 if unknown. */
int picomesh_role_rank(const char *role);

/* Inverse of picomesh_role_rank: the role name for a ladder rank, or NULL if
 * the rank is out of range. */
const char *picomesh_role_name(int rank);

/* Highest role rank held for `account` within a comma-separated groups list
 * ("<account>:<role>,..."). Returns -1 if the account is absent. */
int picomesh_groups_max_role(const char *groups_csv, const char *account);

/* Effective role rank for `namespace_path`, honouring namespace inheritance:
 * the highest direct role held on the namespace OR any of its ancestors. The
 * path is walked from the full namespace up to its root (`acme/platform/api` ->
 * `acme/platform` -> `acme`), so a membership on a parent namespace applies to
 * a child resource. Returns -1 when no applicable membership is held. */
int picomesh_groups_effective_role(const char *groups_csv,
                                   const char *namespace_path);

/* Verified auth context extracted from a JWT. */
struct picomesh_authctx {
  int authenticated; /* 1 if a valid JWT was present */
  uint32_t uid;      /* sub */
  char username[64];
  char groups_csv[512]; /* flattened "<account>:<role>,..." */
};

/* Verify `jwt` under `secret` and fill `out`. On success returns ok=1 with the
 * context populated; on any verification failure returns an error Result and
 * leaves out->authenticated = 0. A NULL/empty jwt yields ok=1 with
 * authenticated=0 (anonymous), not an error. */
struct picomesh_void_result
picomesh_authctx_from_jwt(const char *jwt, const char *secret,
                          struct picomesh_authctx *out);

#ifdef __cplusplus
}
#endif

#endif /* PICOMESH_SECURITY_JWT_H */
