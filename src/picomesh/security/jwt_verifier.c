/* jwt_verifier — fetch signing key once, verify on demand. HS256 today. */

#include <picomesh/security/jwt_verifier.h>
#include <picomesh/security/jwt.h>
#include <picomesh/security/secret.h>

#include <stdlib.h>

struct picomesh_jwt_verifier {
    struct picomesh_engine *engine;
    char *secret; /* lazily loaded, cached for this verifier's life */
};

struct picomesh_void_ptr_result picomesh_jwt_verifier_create(struct picomesh_engine *engine)
{
    struct picomesh_jwt_verifier *verifier = calloc(1, sizeof(*verifier));
    if (!verifier) return PICOMESH_ERR(picomesh_void_ptr, "jwt_verifier_create: out of memory");
    verifier->engine = engine;
    return PICOMESH_OK(picomesh_void_ptr, verifier);
}

struct picomesh_string_result picomesh_jwt_verifier_verify(struct picomesh_jwt_verifier *verifier,
                                                           const char *jwt)
{
    if (!verifier) return PICOMESH_ERR(picomesh_string, "jwt_verifier_verify: NULL verifier");
    if (!jwt || !*jwt) return PICOMESH_ERR(picomesh_string, "jwt_verifier_verify: empty token");

    if (!verifier->secret) {
        struct picomesh_string_result secret = picomesh_security_jwt_secret(verifier->engine);
        if (PICOMESH_IS_ERR(secret))
            return PICOMESH_ERR(picomesh_string, "jwt_verifier_verify: signing key unavailable", secret);
        verifier->secret = secret.value;
    }
    return picomesh_jwt_verify(jwt, verifier->secret, picomesh_security_now());
}

void picomesh_jwt_verifier_destroy(struct picomesh_jwt_verifier *verifier)
{
    if (!verifier) return;
    free(verifier->secret);
    free(verifier);
}
