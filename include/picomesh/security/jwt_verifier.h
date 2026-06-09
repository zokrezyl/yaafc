/* jwt_verifier — the one place that turns a JWT string into verified claims.
 *
 * Both the authenticator chain and the policy authorizer use this helper, so
 * the "fetch the signing key + verify signature + check expiry" step lives in
 * exactly one boring spot. It owns NO policy: it only answers "is this JWT
 * valid, and what are its claims?". Today it verifies HS256 against the shared
 * mesh secret (configured key material); a future asymmetric scheme would
 * fetch a public key here without touching any caller. */

#ifndef PICOMESH_SECURITY_JWT_VERIFIER_H
#define PICOMESH_SECURITY_JWT_VERIFIER_H

#include <picomesh/core/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct picomesh_engine;
struct picomesh_jwt_verifier;

/* Build a verifier bound to `engine`'s configured signing key. */
struct picomesh_void_ptr_result
picomesh_jwt_verifier_create(struct picomesh_engine *engine);

/* Verify `jwt` (signature + expiry). On success the OK value is the malloc'd
 * claims-payload JSON (caller frees). Fails closed on a bad/expired/malformed
 * token or missing key material. */
struct picomesh_string_result
picomesh_jwt_verifier_verify(struct picomesh_jwt_verifier *verifier,
                             const char *jwt);

void picomesh_jwt_verifier_destroy(struct picomesh_jwt_verifier *verifier);

#ifdef __cplusplus
}
#endif

#endif /* PICOMESH_SECURITY_JWT_VERIFIER_H */
