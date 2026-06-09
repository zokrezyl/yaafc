/* JWT signing-secret + TTL accessors, read from engine config. */

#include <picomesh/security/secret.h>
#include <picomesh/security/jwt.h>
#include <picomesh/engine/engine.h>
#include <picomesh/config/config.h>
#include <picomesh/picoclass/yheaders.h>

#include <stdlib.h>
#include <string.h>

struct picomesh_string_result picomesh_security_jwt_secret(struct picomesh_engine *engine)
{
    /* Source 1: config `security.jwt_secret` (env-substituted). This is what a
     * single-process / all-in-one deployment uses. */
    const char *secret = NULL;
    if (engine) {
        const struct config *config = picomesh_engine_config(engine);
        if (config) {
            struct config_node_ptr_result node = config_get(config, "security.jwt_secret");
            if (PICOMESH_IS_OK(node) && node.value) secret = config_node_as_string(node.value, NULL);
            else if (PICOMESH_IS_ERR(node)) picomesh_error_destroy(node.error);
        }
    }
    /* Source 2: the PICOMESH_JWT_SECRET environment variable. The split mesh
     * spawns each backend with a FLATTENED per-node config that carries only
     * that node's `config` block — the top-level `security` key does not reach
     * children there — but the parent's environment IS inherited, so the
     * env-sourced secret reaches every node. */
    if (!secret || !*secret) secret = getenv("PICOMESH_JWT_SECRET");
    if (!secret || !*secret)
        return PICOMESH_ERR(picomesh_string,
                            "security: jwt secret unavailable — set PICOMESH_JWT_SECRET "
                            "(or security.jwt_secret in config)");

    char *copy = strdup(secret);
    if (!copy) return PICOMESH_ERR(picomesh_string, "security: out of memory copying jwt secret");
    return PICOMESH_OK(picomesh_string, copy);
}

struct picomesh_void_result picomesh_authctx_from_headers(struct yheaders *hdrs,
                                                          struct picomesh_engine *engine,
                                                          struct picomesh_authctx *out)
{
    memset(out, 0, sizeof(*out));
    /* Identity is derived ONLY from the verified JWT the gateway forwarded.
     * There is no uid-header fallback: an absent or invalid JWT yields an
     * unauthenticated context (uid 0), and resource checks fail closed. This
     * is low-level crypto glue — it must never make a policy decision such as
     * "trust the uid anyway". */
    const char *jwt = hdrs ? yheaders_get(hdrs, "jwt") : NULL;
    if (!jwt || !*jwt) return PICOMESH_OK_VOID(); /* anonymous */

    struct picomesh_string_result secret = picomesh_security_jwt_secret(engine);
    if (PICOMESH_IS_ERR(secret)) {
        picomesh_error_destroy(secret.error);
        return PICOMESH_OK_VOID(); /* no key → cannot verify → fail closed */
    }
    struct picomesh_void_result verified = picomesh_authctx_from_jwt(jwt, secret.value, out);
    free(secret.value);
    if (PICOMESH_IS_ERR(verified)) {
        picomesh_error_destroy(verified.error);
        memset(out, 0, sizeof(*out)); /* invalid JWT → unauthenticated, fail closed */
    }
    return PICOMESH_OK_VOID();
}

struct picomesh_int64_result picomesh_security_access_ttl(struct picomesh_engine *engine)
{
    const int64_t default_ttl = 900;
    if (!engine) return PICOMESH_OK(picomesh_int64, default_ttl);
    const struct config *config = picomesh_engine_config(engine);
    if (!config) return PICOMESH_OK(picomesh_int64, default_ttl);
    struct config_node_ptr_result node = config_get(config, "security.access_ttl_seconds");
    PICOMESH_RETURN_IF_ERR(picomesh_int64, node, "access_ttl: config_get failed");
    if (!node.value) return PICOMESH_OK(picomesh_int64, default_ttl);
    int64_t ttl = config_node_as_int(node.value, default_ttl);
    return PICOMESH_OK(picomesh_int64, ttl > 0 ? ttl : default_ttl);
}
