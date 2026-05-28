/* GENERATED — do not edit. */
#include "storage.internal.h"
#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/yclass/rpc.h>
#include <stdint.h>
#include <string.h>

struct yaafc_int_result storage_kv_set(struct ctx * ctx, struct object * obj, uint32_t key_id, int32_t value)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("storage", (method_id_t)storage_kv_set);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "storage_kv_set: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "storage_kv_set: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "storage_kv_set: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int, "storage_kv_set: pack overflow");
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
                return YAAFC_ERR(yaafc_int, "storage_kv_set: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(key_id) > sizeof(_a))
            return YAAFC_ERR(yaafc_int, "storage_kv_set: pack overflow");
        memcpy(_a + _off, &key_id, sizeof(key_id)); _off += sizeof(key_id);
        if (_off + sizeof(value) > sizeof(_a))
            return YAAFC_ERR(yaafc_int, "storage_kv_set: pack overflow");
        memcpy(_a + _off, &value, sizeof(value)); _off += sizeof(value);
        uint8_t _wbuf[1 + 4 + 256];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "storage_kv_set: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "storage_kv_set: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "storage_kv_set: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "storage_kv_set: no impl on this class");
        return ((storage_kv_set_fn)fn)(ctx, obj, key_id, value);
    }
}

struct yaafc_int_result storage_kv_get(struct ctx * ctx, struct object * obj, uint32_t key_id)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("storage", (method_id_t)storage_kv_get);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "storage_kv_get: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "storage_kv_get: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "storage_kv_get: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int, "storage_kv_get: pack overflow");
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
                return YAAFC_ERR(yaafc_int, "storage_kv_get: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(key_id) > sizeof(_a))
            return YAAFC_ERR(yaafc_int, "storage_kv_get: pack overflow");
        memcpy(_a + _off, &key_id, sizeof(key_id)); _off += sizeof(key_id);
        uint8_t _wbuf[1 + 4 + 256];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "storage_kv_get: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "storage_kv_get: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "storage_kv_get: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "storage_kv_get: no impl on this class");
        return ((storage_kv_get_fn)fn)(ctx, obj, key_id);
    }
}

struct yaafc_size_result storage_kv_count(struct ctx * ctx, struct object * obj)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("storage", (method_id_t)storage_kv_count);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_size, "storage_kv_count: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_size, "storage_kv_count: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_size, "storage_kv_count: remote id unresolved");
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
                return YAAFC_ERR(yaafc_size, "storage_kv_count: pack overflow");
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
                return YAAFC_ERR(yaafc_size, "storage_kv_count: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        uint8_t _wbuf[1 + 4 + 256];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YAAFC_ERR(yaafc_size, "storage_kv_count: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_size, _msg[0] ? strdup(_msg) : "storage_kv_count: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(size_t)) return YAAFC_ERR(yaafc_size, "storage_kv_count: truncated RPC payload");
        size_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_size, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_size, "storage_kv_count: no impl on this class");
        return ((storage_kv_count_fn)fn)(ctx, obj);
    }
}

struct yaafc_int_result storage_sql_set(struct ctx * ctx, struct object * obj, const char * key, int64_t value)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("storage", (method_id_t)storage_sql_set);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "storage_sql_set: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "storage_sql_set: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "storage_sql_set: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int, "storage_sql_set: pack overflow");
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
                return YAAFC_ERR(yaafc_int, "storage_sql_set: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(key ? strlen(key) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "storage_sql_set: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, key, _slen); _off += _slen; }
        }
        if (_off + sizeof(value) > sizeof(_a))
            return YAAFC_ERR(yaafc_int, "storage_sql_set: pack overflow");
        memcpy(_a + _off, &value, sizeof(value)); _off += sizeof(value);
        uint8_t _wbuf[1 + 4 + 256];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "storage_sql_set: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "storage_sql_set: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "storage_sql_set: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "storage_sql_set: no impl on this class");
        return ((storage_sql_set_fn)fn)(ctx, obj, key, value);
    }
}

struct yaafc_int64_result storage_sql_get(struct ctx * ctx, struct object * obj, const char * key)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("storage", (method_id_t)storage_sql_get);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int64, "storage_sql_get: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int64, "storage_sql_get: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int64, "storage_sql_get: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int64, "storage_sql_get: pack overflow");
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
                return YAAFC_ERR(yaafc_int64, "storage_sql_get: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(key ? strlen(key) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_int64, "storage_sql_get: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, key, _slen); _off += _slen; }
        }
        uint8_t _wbuf[1 + 4 + 256];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YAAFC_ERR(yaafc_int64, "storage_sql_get: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int64, _msg[0] ? strdup(_msg) : "storage_sql_get: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int64_t)) return YAAFC_ERR(yaafc_int64, "storage_sql_get: truncated RPC payload");
        int64_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int64, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int64, "storage_sql_get: no impl on this class");
        return ((storage_sql_get_fn)fn)(ctx, obj, key);
    }
}

struct yaafc_int_result storage_sql_exists(struct ctx * ctx, struct object * obj, const char * key)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("storage", (method_id_t)storage_sql_exists);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "storage_sql_exists: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "storage_sql_exists: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "storage_sql_exists: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int, "storage_sql_exists: pack overflow");
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
                return YAAFC_ERR(yaafc_int, "storage_sql_exists: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(key ? strlen(key) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "storage_sql_exists: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, key, _slen); _off += _slen; }
        }
        uint8_t _wbuf[1 + 4 + 256];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "storage_sql_exists: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "storage_sql_exists: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "storage_sql_exists: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "storage_sql_exists: no impl on this class");
        return ((storage_sql_exists_fn)fn)(ctx, obj, key);
    }
}

struct yaafc_int_result storage_sql_del(struct ctx * ctx, struct object * obj, const char * key)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("storage", (method_id_t)storage_sql_del);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "storage_sql_del: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "storage_sql_del: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "storage_sql_del: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int, "storage_sql_del: pack overflow");
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
                return YAAFC_ERR(yaafc_int, "storage_sql_del: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(key ? strlen(key) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "storage_sql_del: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, key, _slen); _off += _slen; }
        }
        uint8_t _wbuf[1 + 4 + 256];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "storage_sql_del: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "storage_sql_del: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "storage_sql_del: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "storage_sql_del: no impl on this class");
        return ((storage_sql_del_fn)fn)(ctx, obj, key);
    }
}

struct yaafc_size_result storage_sql_count(struct ctx * ctx, struct object * obj)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("storage", (method_id_t)storage_sql_count);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_size, "storage_sql_count: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_size, "storage_sql_count: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_size, "storage_sql_count: remote id unresolved");
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
                return YAAFC_ERR(yaafc_size, "storage_sql_count: pack overflow");
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
                return YAAFC_ERR(yaafc_size, "storage_sql_count: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        uint8_t _wbuf[1 + 4 + 256];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YAAFC_ERR(yaafc_size, "storage_sql_count: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_size, _msg[0] ? strdup(_msg) : "storage_sql_count: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(size_t)) return YAAFC_ERR(yaafc_size, "storage_sql_count: truncated RPC payload");
        size_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_size, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_size, "storage_sql_count: no impl on this class");
        return ((storage_sql_count_fn)fn)(ctx, obj);
    }
}

