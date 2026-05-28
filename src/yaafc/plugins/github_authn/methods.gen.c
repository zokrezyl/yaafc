/* GENERATED — do not edit. */
#include "github_authn.internal.h"
#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/yclass/rpc.h>
#include <stdint.h>
#include <string.h>

struct yaafc_int_result github_authn_store_set_credentials(struct ctx * ctx, struct object * obj, uint32_t client_id, uint32_t secret_id)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("github_authn", (method_id_t)github_authn_store_set_credentials);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "github_authn_store_set_credentials: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "github_authn_store_set_credentials: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "github_authn_store_set_credentials: remote id unresolved");
        uint8_t _a[16384];
        size_t _off = 0;
        /* Caller-auth prefix: every backend yrpc body starts with the
         * (uid, sid) of the gateway-resolved caller. The skel pops
         * these into its local ctx before unpacking args. (For the
         * HTTP /_rpc shim, the gateway translates Cookie/Bearer to
         * (uid, sid) and emits the exact same yrpc body downstream.) */
        {
            uint32_t _u = _s->uid, _i = _s->sid;
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "github_authn_store_set_credentials: pack overflow");
            memcpy(_a + _off, &_u, 4); _off += 4;
            memcpy(_a + _off, &_i, 4); _off += 4;
        }
        /* Also stamp the session in case it's HTTP-mode (the gateway's
         * outbound, if it ever needs to talk HTTP). Cheap no-op for
         * TCP-mode sessions. */
        rpc_session_set_auth(_s->session, _s->uid, _s->sid);
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "github_authn_store_set_credentials: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(client_id) > sizeof(_a))
            return YAAFC_ERR(yaafc_int, "github_authn_store_set_credentials: pack overflow");
        memcpy(_a + _off, &client_id, sizeof(client_id)); _off += sizeof(client_id);
        if (_off + sizeof(secret_id) > sizeof(_a))
            return YAAFC_ERR(yaafc_int, "github_authn_store_set_credentials: pack overflow");
        memcpy(_a + _off, &secret_id, sizeof(secret_id)); _off += sizeof(secret_id);
        uint8_t _wbuf[1 + 4 + 256];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "github_authn_store_set_credentials: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "github_authn_store_set_credentials: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "github_authn_store_set_credentials: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "github_authn_store_set_credentials: no impl on this class");
        return ((github_authn_store_set_credentials_fn)fn)(ctx, obj, client_id, secret_id);
    }
}

struct yaafc_int_result github_authn_store_register_code(struct ctx * ctx, struct object * obj, uint32_t code, uint32_t user_id)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("github_authn", (method_id_t)github_authn_store_register_code);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "github_authn_store_register_code: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "github_authn_store_register_code: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "github_authn_store_register_code: remote id unresolved");
        uint8_t _a[16384];
        size_t _off = 0;
        /* Caller-auth prefix: every backend yrpc body starts with the
         * (uid, sid) of the gateway-resolved caller. The skel pops
         * these into its local ctx before unpacking args. (For the
         * HTTP /_rpc shim, the gateway translates Cookie/Bearer to
         * (uid, sid) and emits the exact same yrpc body downstream.) */
        {
            uint32_t _u = _s->uid, _i = _s->sid;
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "github_authn_store_register_code: pack overflow");
            memcpy(_a + _off, &_u, 4); _off += 4;
            memcpy(_a + _off, &_i, 4); _off += 4;
        }
        /* Also stamp the session in case it's HTTP-mode (the gateway's
         * outbound, if it ever needs to talk HTTP). Cheap no-op for
         * TCP-mode sessions. */
        rpc_session_set_auth(_s->session, _s->uid, _s->sid);
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "github_authn_store_register_code: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(code) > sizeof(_a))
            return YAAFC_ERR(yaafc_int, "github_authn_store_register_code: pack overflow");
        memcpy(_a + _off, &code, sizeof(code)); _off += sizeof(code);
        if (_off + sizeof(user_id) > sizeof(_a))
            return YAAFC_ERR(yaafc_int, "github_authn_store_register_code: pack overflow");
        memcpy(_a + _off, &user_id, sizeof(user_id)); _off += sizeof(user_id);
        uint8_t _wbuf[1 + 4 + 256];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "github_authn_store_register_code: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "github_authn_store_register_code: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "github_authn_store_register_code: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "github_authn_store_register_code: no impl on this class");
        return ((github_authn_store_register_code_fn)fn)(ctx, obj, code, user_id);
    }
}

struct yaafc_uint32_result github_authn_store_resolve(struct ctx * ctx, struct object * obj, uint32_t code)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("github_authn", (method_id_t)github_authn_store_resolve);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_uint32, "github_authn_store_resolve: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_uint32, "github_authn_store_resolve: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_uint32, "github_authn_store_resolve: remote id unresolved");
        uint8_t _a[16384];
        size_t _off = 0;
        /* Caller-auth prefix: every backend yrpc body starts with the
         * (uid, sid) of the gateway-resolved caller. The skel pops
         * these into its local ctx before unpacking args. (For the
         * HTTP /_rpc shim, the gateway translates Cookie/Bearer to
         * (uid, sid) and emits the exact same yrpc body downstream.) */
        {
            uint32_t _u = _s->uid, _i = _s->sid;
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_uint32, "github_authn_store_resolve: pack overflow");
            memcpy(_a + _off, &_u, 4); _off += 4;
            memcpy(_a + _off, &_i, 4); _off += 4;
        }
        /* Also stamp the session in case it's HTTP-mode (the gateway's
         * outbound, if it ever needs to talk HTTP). Cheap no-op for
         * TCP-mode sessions. */
        rpc_session_set_auth(_s->session, _s->uid, _s->sid);
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_uint32, "github_authn_store_resolve: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(code) > sizeof(_a))
            return YAAFC_ERR(yaafc_uint32, "github_authn_store_resolve: pack overflow");
        memcpy(_a + _off, &code, sizeof(code)); _off += sizeof(code);
        uint8_t _wbuf[1 + 4 + 256];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YAAFC_ERR(yaafc_uint32, "github_authn_store_resolve: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_uint32, _msg[0] ? strdup(_msg) : "github_authn_store_resolve: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(uint32_t)) return YAAFC_ERR(yaafc_uint32, "github_authn_store_resolve: truncated RPC payload");
        uint32_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_uint32, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_uint32, "github_authn_store_resolve: no impl on this class");
        return ((github_authn_store_resolve_fn)fn)(ctx, obj, code);
    }
}

struct yaafc_size_result github_authn_store_count_codes(struct ctx * ctx, struct object * obj)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("github_authn", (method_id_t)github_authn_store_count_codes);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_size, "github_authn_store_count_codes: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_size, "github_authn_store_count_codes: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_size, "github_authn_store_count_codes: remote id unresolved");
        uint8_t _a[16384];
        size_t _off = 0;
        /* Caller-auth prefix: every backend yrpc body starts with the
         * (uid, sid) of the gateway-resolved caller. The skel pops
         * these into its local ctx before unpacking args. (For the
         * HTTP /_rpc shim, the gateway translates Cookie/Bearer to
         * (uid, sid) and emits the exact same yrpc body downstream.) */
        {
            uint32_t _u = _s->uid, _i = _s->sid;
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_size, "github_authn_store_count_codes: pack overflow");
            memcpy(_a + _off, &_u, 4); _off += 4;
            memcpy(_a + _off, &_i, 4); _off += 4;
        }
        /* Also stamp the session in case it's HTTP-mode (the gateway's
         * outbound, if it ever needs to talk HTTP). Cheap no-op for
         * TCP-mode sessions. */
        rpc_session_set_auth(_s->session, _s->uid, _s->sid);
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_size, "github_authn_store_count_codes: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        uint8_t _wbuf[1 + 4 + 256];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YAAFC_ERR(yaafc_size, "github_authn_store_count_codes: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_size, _msg[0] ? strdup(_msg) : "github_authn_store_count_codes: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(size_t)) return YAAFC_ERR(yaafc_size, "github_authn_store_count_codes: truncated RPC payload");
        size_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_size, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_size, "github_authn_store_count_codes: no impl on this class");
        return ((github_authn_store_count_codes_fn)fn)(ctx, obj);
    }
}

