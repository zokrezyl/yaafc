/* GENERATED — do not edit. */
#include "git_repo.internal.h"
#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/yclass/rpc.h>
#include <stdint.h>
#include <string.h>

struct yaafc_uint32_result git_repo_store_make(struct ctx * ctx, struct object * obj, uint32_t owner_id)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("git_repo", (method_id_t)git_repo_store_make);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_uint32, "git_repo_store_make: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_uint32, "git_repo_store_make: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_uint32, "git_repo_store_make: remote id unresolved");
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
                return YAAFC_ERR(yaafc_uint32, "git_repo_store_make: pack overflow");
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
                return YAAFC_ERR(yaafc_uint32, "git_repo_store_make: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(owner_id) > sizeof(_a))
            return YAAFC_ERR(yaafc_uint32, "git_repo_store_make: pack overflow");
        memcpy(_a + _off, &owner_id, sizeof(owner_id)); _off += sizeof(owner_id);
        uint8_t _wbuf[1 + 4 + 256];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YAAFC_ERR(yaafc_uint32, "git_repo_store_make: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_uint32, _msg[0] ? strdup(_msg) : "git_repo_store_make: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(uint32_t)) return YAAFC_ERR(yaafc_uint32, "git_repo_store_make: truncated RPC payload");
        uint32_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_uint32, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_uint32, "git_repo_store_make: no impl on this class");
        return ((git_repo_store_make_fn)fn)(ctx, obj, owner_id);
    }
}

struct yaafc_int_result git_repo_store_delete(struct ctx * ctx, struct object * obj, uint32_t repo_id)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("git_repo", (method_id_t)git_repo_store_delete);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "git_repo_store_delete: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "git_repo_store_delete: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "git_repo_store_delete: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int, "git_repo_store_delete: pack overflow");
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
                return YAAFC_ERR(yaafc_int, "git_repo_store_delete: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(repo_id) > sizeof(_a))
            return YAAFC_ERR(yaafc_int, "git_repo_store_delete: pack overflow");
        memcpy(_a + _off, &repo_id, sizeof(repo_id)); _off += sizeof(repo_id);
        uint8_t _wbuf[1 + 4 + 256];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "git_repo_store_delete: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "git_repo_store_delete: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "git_repo_store_delete: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "git_repo_store_delete: no impl on this class");
        return ((git_repo_store_delete_fn)fn)(ctx, obj, repo_id);
    }
}

struct yaafc_uint32_result git_repo_store_owner_of(struct ctx * ctx, struct object * obj, uint32_t repo_id)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("git_repo", (method_id_t)git_repo_store_owner_of);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_uint32, "git_repo_store_owner_of: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_uint32, "git_repo_store_owner_of: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_uint32, "git_repo_store_owner_of: remote id unresolved");
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
                return YAAFC_ERR(yaafc_uint32, "git_repo_store_owner_of: pack overflow");
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
                return YAAFC_ERR(yaafc_uint32, "git_repo_store_owner_of: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(repo_id) > sizeof(_a))
            return YAAFC_ERR(yaafc_uint32, "git_repo_store_owner_of: pack overflow");
        memcpy(_a + _off, &repo_id, sizeof(repo_id)); _off += sizeof(repo_id);
        uint8_t _wbuf[1 + 4 + 256];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YAAFC_ERR(yaafc_uint32, "git_repo_store_owner_of: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_uint32, _msg[0] ? strdup(_msg) : "git_repo_store_owner_of: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(uint32_t)) return YAAFC_ERR(yaafc_uint32, "git_repo_store_owner_of: truncated RPC payload");
        uint32_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_uint32, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_uint32, "git_repo_store_owner_of: no impl on this class");
        return ((git_repo_store_owner_of_fn)fn)(ctx, obj, repo_id);
    }
}

struct yaafc_size_result git_repo_store_count_for_owner(struct ctx * ctx, struct object * obj, uint32_t owner_id)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("git_repo", (method_id_t)git_repo_store_count_for_owner);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_size, "git_repo_store_count_for_owner: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_size, "git_repo_store_count_for_owner: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_size, "git_repo_store_count_for_owner: remote id unresolved");
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
                return YAAFC_ERR(yaafc_size, "git_repo_store_count_for_owner: pack overflow");
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
                return YAAFC_ERR(yaafc_size, "git_repo_store_count_for_owner: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(owner_id) > sizeof(_a))
            return YAAFC_ERR(yaafc_size, "git_repo_store_count_for_owner: pack overflow");
        memcpy(_a + _off, &owner_id, sizeof(owner_id)); _off += sizeof(owner_id);
        uint8_t _wbuf[1 + 4 + 256];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YAAFC_ERR(yaafc_size, "git_repo_store_count_for_owner: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_size, _msg[0] ? strdup(_msg) : "git_repo_store_count_for_owner: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(size_t)) return YAAFC_ERR(yaafc_size, "git_repo_store_count_for_owner: truncated RPC payload");
        size_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_size, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_size, "git_repo_store_count_for_owner: no impl on this class");
        return ((git_repo_store_count_for_owner_fn)fn)(ctx, obj, owner_id);
    }
}

struct yaafc_size_result git_repo_store_count_total(struct ctx * ctx, struct object * obj)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("git_repo", (method_id_t)git_repo_store_count_total);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_size, "git_repo_store_count_total: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_size, "git_repo_store_count_total: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_size, "git_repo_store_count_total: remote id unresolved");
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
                return YAAFC_ERR(yaafc_size, "git_repo_store_count_total: pack overflow");
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
                return YAAFC_ERR(yaafc_size, "git_repo_store_count_total: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        uint8_t _wbuf[1 + 4 + 256];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YAAFC_ERR(yaafc_size, "git_repo_store_count_total: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_size, _msg[0] ? strdup(_msg) : "git_repo_store_count_total: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(size_t)) return YAAFC_ERR(yaafc_size, "git_repo_store_count_total: truncated RPC payload");
        size_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_size, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_size, "git_repo_store_count_total: no impl on this class");
        return ((git_repo_store_count_total_fn)fn)(ctx, obj);
    }
}

