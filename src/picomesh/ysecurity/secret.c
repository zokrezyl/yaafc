/* JWT signing-secret + TTL accessors, read from engine config. */

#include <picomesh/ysecurity/secret.h>
#include <picomesh/ysecurity/jwt.h>
#include <picomesh/yengine/engine.h>
#include <picomesh/yconfig/yconfig.h>
#include <picomesh/yclass/yheaders.h>

#include <stdlib.h>
#include <string.h>

struct picomesh_string_result picomesh_security_jwt_secret(struct picomesh_engine *engine)
{
    /* Source 1: config `security.jwt_secret` (env-substituted). This is what a
     * single-process / all-in-one deployment uses. */
    const char *secret = NULL;
    if (engine) {
        const struct yconfig *config = picomesh_engine_config(engine);
        if (config) {
            struct yconfig_node_ptr_result node = yconfig_get(config, "security.jwt_secret");
            if (PICOMESH_IS_OK(node) && node.value) secret = yconfig_node_as_string(node.value, NULL);
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
    const char *jwt = hdrs ? yheaders_get(hdrs, "jwt") : NULL;
    if (jwt && *jwt) {
        struct picomesh_string_result secret = picomesh_security_jwt_secret(engine);
        if (PICOMESH_IS_OK(secret)) {
            struct picomesh_void_result a = picomesh_authctx_from_jwt(jwt, secret.value, out);
            free(secret.value);
            if (PICOMESH_IS_OK(a) && out->authenticated) return PICOMESH_OK_VOID();
            if (PICOMESH_IS_ERR(a)) picomesh_error_destroy(a.error);
            memset(out, 0, sizeof(*out)); /* invalid jwt → fall back to uid header */
        } else {
            picomesh_error_destroy(secret.error);
        }
    }
    /* Fallback: trust the gateway-set uid (the gateway already verified it).
     * No verifiable groups are available locally in this case. */
    out->uid = hdrs ? yheaders_get_u32(hdrs, "uid", 0) : 0;
    out->authenticated = 0;
    return PICOMESH_OK_VOID();
}

int64_t picomesh_security_access_ttl(struct picomesh_engine *engine)
{
    const int64_t default_ttl = 900;
    if (!engine) return default_ttl;
    const struct yconfig *config = picomesh_engine_config(engine);
    if (!config) return default_ttl;
    struct yconfig_node_ptr_result node = yconfig_get(config, "security.access_ttl_seconds");
    if (PICOMESH_IS_ERR(node)) { picomesh_error_destroy(node.error); return default_ttl; }
    if (!node.value) return default_ttl;
    int64_t ttl = yconfig_node_as_int(node.value, default_ttl);
    return ttl > 0 ? ttl : default_ttl;
}
