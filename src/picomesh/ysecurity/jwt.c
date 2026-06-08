/* HS256 JWT encode/verify + auth-context extraction. */

#include <picomesh/ysecurity/jwt.h>
#include <picomesh/ysecurity/base64.h>
#include <picomesh/ysecurity/sha256.h>
#include <picomesh/yjson/yjson.h>
#include <picomesh/yplatform/time.h>

#include <stdlib.h>
#include <string.h>

/* The fixed HS256 header, and its base64url form (precomputed at first use as
 * a function-local static — no file-scope data symbol). */
static const char *jwt_header_b64(void)
{
    /* {"alg":"HS256","typ":"JWT"} */
    static char encoded[64];
    static int ready = 0;
    if (!ready) {
        const char header[] = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
        picomesh_base64url_encode(header, sizeof(header) - 1, encoded, sizeof(encoded));
        ready = 1;
    }
    return encoded;
}

int64_t picomesh_security_now(void)
{
    return picomesh_yplatform_time_wall_ms() / 1000;
}

char *picomesh_jwt_build_claims(const char *issuer, uint32_t sub, const char *username,
                                const char *groups_csv, int64_t issued_at, int64_t expires_at)
{
    struct yjson_writer *writer = yjson_writer_new();
    if (!writer) return NULL;
    yjson_writer_begin_object(writer);
    yjson_writer_key(writer, "iss");      yjson_writer_string(writer, issuer ? issuer : "picomesh");
    yjson_writer_key(writer, "sub");      yjson_writer_int(writer, (int64_t)sub);
    yjson_writer_key(writer, "username"); yjson_writer_string(writer, username ? username : "");
    yjson_writer_key(writer, "groups");   yjson_writer_begin_array(writer);
    if (groups_csv && *groups_csv) {
        const char *cursor = groups_csv;
        while (*cursor) {
            const char *comma = strchr(cursor, ',');
            size_t span = comma ? (size_t)(comma - cursor) : strlen(cursor);
            if (span > 0) {
                char group[128];
                if (span >= sizeof(group)) span = sizeof(group) - 1;
                memcpy(group, cursor, span);
                group[span] = 0;
                yjson_writer_string(writer, group);
            }
            if (!comma) break;
            cursor = comma + 1;
        }
    }
    yjson_writer_end_array(writer);
    yjson_writer_key(writer, "iat"); yjson_writer_int(writer, issued_at);
    yjson_writer_key(writer, "exp"); yjson_writer_int(writer, expires_at);
    /* jti: a per-token identifier. Not load-bearing for HS256 verification;
     * derived from sub + iat so it is stable per mint without extra entropy. */
    char jti[48];
    snprintf(jti, sizeof(jti), "%u-%lld", sub, (long long)issued_at);
    yjson_writer_key(writer, "jti"); yjson_writer_string(writer, jti);
    yjson_writer_end_object(writer);

    size_t len = 0;
    const char *data = yjson_writer_data(writer, &len);
    char *out = data ? strdup(data) : NULL;
    yjson_writer_free(writer);
    return out;
}

char *picomesh_jwt_encode(const char *claims_json, const char *secret)
{
    if (!claims_json || !secret) return NULL;
    const char *header_segment = jwt_header_b64();
    size_t header_len = strlen(header_segment);

    size_t claims_len = strlen(claims_json);
    /* payload base64url is at most claims_len*4/3 + 2. */
    size_t payload_cap = claims_len * 4 / 3 + 4;
    char *payload_segment = malloc(payload_cap);
    if (!payload_segment) return NULL;
    size_t payload_len = picomesh_base64url_encode(claims_json, claims_len, payload_segment, payload_cap);
    if (payload_len == 0) { free(payload_segment); return NULL; }

    /* signing input = header "." payload */
    size_t signing_len = header_len + 1 + payload_len;
    char *signing_input = malloc(signing_len + 1);
    if (!signing_input) { free(payload_segment); return NULL; }
    memcpy(signing_input, header_segment, header_len);
    signing_input[header_len] = '.';
    memcpy(signing_input + header_len + 1, payload_segment, payload_len + 1);
    free(payload_segment);

    uint8_t mac[PICOMESH_SHA256_DIGEST_LEN];
    picomesh_hmac_sha256(secret, strlen(secret), signing_input, signing_len, mac);
    char signature_segment[48];
    size_t signature_len = picomesh_base64url_encode(mac, sizeof(mac), signature_segment, sizeof(signature_segment));
    if (signature_len == 0) { free(signing_input); return NULL; }

    size_t token_len = signing_len + 1 + signature_len;
    char *token = malloc(token_len + 1);
    if (!token) { free(signing_input); return NULL; }
    memcpy(token, signing_input, signing_len);
    token[signing_len] = '.';
    memcpy(token + signing_len + 1, signature_segment, signature_len + 1);
    free(signing_input);
    return token;
}

/* Constant-time comparison of two NUL-terminated strings of equal length.
 * Length is compared first (not secret), then bytes without early-out. */
static int constant_time_equal(const char *a, size_t a_len, const char *b, size_t b_len)
{
    if (a_len != b_len) return 0;
    unsigned char diff = 0;
    for (size_t i = 0; i < a_len; ++i)
        diff |= (unsigned char)(a[i] ^ b[i]);
    return diff == 0;
}

struct picomesh_string_result picomesh_jwt_verify(const char *jwt, const char *secret, int64_t now)
{
    if (!jwt || !*jwt || !secret)
        return PICOMESH_ERR(picomesh_string, "jwt_verify: missing token or secret");

    const char *first_dot = strchr(jwt, '.');
    if (!first_dot) return PICOMESH_ERR(picomesh_string, "jwt_verify: malformed (no header.payload)");
    const char *second_dot = strchr(first_dot + 1, '.');
    if (!second_dot) return PICOMESH_ERR(picomesh_string, "jwt_verify: malformed (no signature)");

    const char *header_segment = jwt;
    size_t header_len = (size_t)(first_dot - jwt);
    const char *payload_segment = first_dot + 1;
    size_t payload_seg_len = (size_t)(second_dot - payload_segment);
    const char *signature_segment = second_dot + 1;
    size_t signature_len = strlen(signature_segment);

    /* Recompute the signature over "header.payload" and compare. */
    size_t signing_len = (size_t)(second_dot - jwt);
    uint8_t mac[PICOMESH_SHA256_DIGEST_LEN];
    picomesh_hmac_sha256(secret, strlen(secret), jwt, signing_len, mac);
    char expected_signature[48];
    size_t expected_len = picomesh_base64url_encode(mac, sizeof(mac), expected_signature, sizeof(expected_signature));
    if (expected_len == 0)
        return PICOMESH_ERR(picomesh_string, "jwt_verify: signature encode failed");
    if (!constant_time_equal(signature_segment, signature_len, expected_signature, expected_len))
        return PICOMESH_ERR(picomesh_string, "jwt_verify: bad signature");

    /* Decode + check the header alg — reject anything but HS256 (defends
     * against alg=none and algorithm-confusion). */
    char header_text[128];
    {
        char header_copy[96];
        if (header_len >= sizeof(header_copy))
            return PICOMESH_ERR(picomesh_string, "jwt_verify: header too long");
        memcpy(header_copy, header_segment, header_len);
        header_copy[header_len] = 0;
        size_t decoded = picomesh_base64url_decode(header_copy, (uint8_t *)header_text, sizeof(header_text) - 1);
        if (decoded == (size_t)-1)
            return PICOMESH_ERR(picomesh_string, "jwt_verify: header decode failed");
        header_text[decoded] = 0;
    }
    struct yjson_doc *header_doc = yjson_parse(header_text, strlen(header_text));
    if (!header_doc) return PICOMESH_ERR(picomesh_string, "jwt_verify: header not JSON");
    const char *alg = yjson_as_string(yjson_object_get(yjson_doc_root(header_doc), "alg"), "");
    int alg_ok = strcmp(alg, "HS256") == 0;
    yjson_doc_free(header_doc);
    if (!alg_ok) return PICOMESH_ERR(picomesh_string, "jwt_verify: unsupported alg");

    /* Decode the payload. */
    char *payload_copy = malloc(payload_seg_len + 1);
    if (!payload_copy) return PICOMESH_ERR(picomesh_string, "jwt_verify: out of memory");
    memcpy(payload_copy, payload_segment, payload_seg_len);
    payload_copy[payload_seg_len] = 0;
    size_t payload_cap = payload_seg_len; /* decoded is always shorter than encoded */
    char *payload_json = malloc(payload_cap + 1);
    if (!payload_json) { free(payload_copy); return PICOMESH_ERR(picomesh_string, "jwt_verify: out of memory"); }
    size_t payload_len = picomesh_base64url_decode(payload_copy, (uint8_t *)payload_json, payload_cap);
    free(payload_copy);
    if (payload_len == (size_t)-1) { free(payload_json); return PICOMESH_ERR(picomesh_string, "jwt_verify: payload decode failed"); }
    payload_json[payload_len] = 0;

    /* Check expiry. */
    struct yjson_doc *payload_doc = yjson_parse(payload_json, payload_len);
    if (!payload_doc) { free(payload_json); return PICOMESH_ERR(picomesh_string, "jwt_verify: payload not JSON"); }
    /* `exp` is mandatory: a token with a valid signature but no expiry must NOT
     * be trusted indefinitely, regardless of whether the issuer always emits it.
     * Treat a missing/zero/negative exp as a verification failure. */
    int64_t exp = yjson_as_int(yjson_object_get(yjson_doc_root(payload_doc), "exp"), 0);
    yjson_doc_free(payload_doc);
    if (exp <= 0) { free(payload_json); return PICOMESH_ERR(picomesh_string, "jwt_verify: missing exp claim"); }
    if (now > exp) { free(payload_json); return PICOMESH_ERR(picomesh_string, "jwt_verify: expired"); }

    return PICOMESH_OK(picomesh_string, payload_json);
}

int picomesh_groups_contains(const char *groups_csv, const char *group)
{
    if (!groups_csv || !group || !*group) return 0;
    size_t glen = strlen(group);
    const char *cursor = groups_csv;
    while (*cursor) {
        const char *comma = strchr(cursor, ',');
        size_t span = comma ? (size_t)(comma - cursor) : strlen(cursor);
        if (span == glen && memcmp(cursor, group, glen) == 0) return 1;
        if (!comma) break;
        cursor = comma + 1;
    }
    return 0;
}

int picomesh_role_rank(const char *role)
{
    if (!role) return -1;
    if (strcmp(role, "guest") == 0) return 0;
    if (strcmp(role, "reporter") == 0) return 1;
    if (strcmp(role, "developer") == 0) return 2;
    if (strcmp(role, "maintainer") == 0) return 3;
    if (strcmp(role, "owner") == 0) return 4;
    return -1;
}

const char *picomesh_role_name(int rank)
{
    switch (rank) {
    case 0: return "guest";
    case 1: return "reporter";
    case 2: return "developer";
    case 3: return "maintainer";
    case 4: return "owner";
    default: return NULL;
    }
}

int picomesh_groups_max_role(const char *groups_csv, const char *account)
{
    if (!groups_csv || !account) return -1;
    size_t account_len = strlen(account);
    int best = -1;
    const char *cursor = groups_csv;
    while (*cursor) {
        const char *comma = strchr(cursor, ',');
        size_t span = comma ? (size_t)(comma - cursor) : strlen(cursor);
        /* Each entry is "<account>:<role>". */
        const char *colon = memchr(cursor, ':', span);
        if (colon) {
            size_t slug_len = (size_t)(colon - cursor);
            if (slug_len == account_len && memcmp(cursor, account, account_len) == 0) {
                char role[32];
                size_t role_len = span - slug_len - 1;
                if (role_len < sizeof(role)) {
                    memcpy(role, colon + 1, role_len);
                    role[role_len] = 0;
                    int rank = picomesh_role_rank(role);
                    if (rank > best) best = rank;
                }
            }
        }
        if (!comma) break;
        cursor = comma + 1;
    }
    return best;
}

int picomesh_groups_effective_role(const char *groups_csv, const char *namespace_path)
{
    if (!groups_csv || !namespace_path || !*namespace_path) return -1;
    /* Namespace inheritance: a membership on any ancestor namespace applies to
     * a child. Walk the path from the full namespace up to its root, taking the
     * highest direct role found at any level. `acme/platform/api` is checked as
     * `acme/platform/api`, then `acme/platform`, then `acme`. The membership
     * strings in the claim are "<namespace-path>:<role>", so an ancestor
     * grant like `acme:developer` satisfies a child resource. Monotonic
     * max-role semantics — no deny rules or inheritance locks yet. */
    char prefix[256];
    int best = -1;
    for (const char *end = namespace_path + strlen(namespace_path); end > namespace_path; ) {
        size_t len = (size_t)(end - namespace_path);
        if (len < sizeof(prefix)) {
            memcpy(prefix, namespace_path, len);
            prefix[len] = 0;
            int rank = picomesh_groups_max_role(groups_csv, prefix);
            if (rank > best) best = rank;
        }
        /* Step up to the parent: drop the last `/`-delimited segment. */
        const char *slash = NULL;
        for (const char *p = end - 1; p >= namespace_path; --p)
            if (*p == '/') { slash = p; break; }
        if (!slash) break;
        end = slash;
    }
    return best;
}

struct picomesh_void_result picomesh_authctx_from_jwt(const char *jwt, const char *secret,
                                                      struct picomesh_authctx *out)
{
    memset(out, 0, sizeof(*out));
    if (!jwt || !*jwt) return PICOMESH_OK_VOID(); /* anonymous, not an error */

    struct picomesh_string_result verified = picomesh_jwt_verify(jwt, secret, picomesh_security_now());
    if (PICOMESH_IS_ERR(verified))
        return PICOMESH_ERR(picomesh_void, "authctx: jwt verification failed", verified);

    struct yjson_doc *doc = yjson_parse(verified.value, strlen(verified.value));
    if (!doc) { free(verified.value); return PICOMESH_ERR(picomesh_void, "authctx: claims not JSON"); }
    const struct yjson_value *root = yjson_doc_root(doc);
    out->uid = (uint32_t)yjson_as_int(yjson_object_get(root, "sub"), 0);
    const char *username = yjson_as_string(yjson_object_get(root, "username"), "");
    snprintf(out->username, sizeof(out->username), "%s", username);

    /* Flatten the groups array to a CSV the role helpers consume. */
    const struct yjson_value *groups = yjson_object_get(root, "groups");
    size_t group_count = groups ? yjson_array_size(groups) : 0;
    size_t pos = 0;
    for (size_t i = 0; i < group_count; ++i) {
        const char *group = yjson_as_string(yjson_array_at(groups, i), NULL);
        if (!group || !*group) continue;
        int written = snprintf(out->groups_csv + pos, sizeof(out->groups_csv) - pos,
                               "%s%s", pos ? "," : "", group);
        if (written < 0 || (size_t)written >= sizeof(out->groups_csv) - pos) break;
        pos += (size_t)written;
    }
    yjson_doc_free(doc);
    free(verified.value);
    out->authenticated = 1;
    return PICOMESH_OK_VOID();
}
