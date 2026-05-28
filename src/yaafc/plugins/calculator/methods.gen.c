/* GENERATED — do not edit. */
#include "calculator.internal.h"
#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/yclass/rpc.h>
#include <stdint.h>
#include <string.h>

struct yaafc_int64_result calculator_calc_add(struct ctx * ctx, struct object * obj, int64_t x, int64_t y)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("calculator", (method_id_t)calculator_calc_add);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int64, "calculator_calc_add: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int64, "calculator_calc_add: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int64, "calculator_calc_add: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int64, "calculator_calc_add: pack overflow");
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
                return YAAFC_ERR(yaafc_int64, "calculator_calc_add: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(x) > sizeof(_a))
            return YAAFC_ERR(yaafc_int64, "calculator_calc_add: pack overflow");
        memcpy(_a + _off, &x, sizeof(x)); _off += sizeof(x);
        if (_off + sizeof(y) > sizeof(_a))
            return YAAFC_ERR(yaafc_int64, "calculator_calc_add: pack overflow");
        memcpy(_a + _off, &y, sizeof(y)); _off += sizeof(y);
        uint8_t _wbuf[1 + 4 + 256];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YAAFC_ERR(yaafc_int64, "calculator_calc_add: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int64, _msg[0] ? strdup(_msg) : "calculator_calc_add: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int64_t)) return YAAFC_ERR(yaafc_int64, "calculator_calc_add: truncated RPC payload");
        int64_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int64, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int64, "calculator_calc_add: no impl on this class");
        return ((calculator_calc_add_fn)fn)(ctx, obj, x, y);
    }
}

struct yaafc_int64_result calculator_calc_sub(struct ctx * ctx, struct object * obj, int64_t x, int64_t y)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("calculator", (method_id_t)calculator_calc_sub);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int64, "calculator_calc_sub: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int64, "calculator_calc_sub: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int64, "calculator_calc_sub: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int64, "calculator_calc_sub: pack overflow");
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
                return YAAFC_ERR(yaafc_int64, "calculator_calc_sub: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(x) > sizeof(_a))
            return YAAFC_ERR(yaafc_int64, "calculator_calc_sub: pack overflow");
        memcpy(_a + _off, &x, sizeof(x)); _off += sizeof(x);
        if (_off + sizeof(y) > sizeof(_a))
            return YAAFC_ERR(yaafc_int64, "calculator_calc_sub: pack overflow");
        memcpy(_a + _off, &y, sizeof(y)); _off += sizeof(y);
        uint8_t _wbuf[1 + 4 + 256];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YAAFC_ERR(yaafc_int64, "calculator_calc_sub: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int64, _msg[0] ? strdup(_msg) : "calculator_calc_sub: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int64_t)) return YAAFC_ERR(yaafc_int64, "calculator_calc_sub: truncated RPC payload");
        int64_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int64, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int64, "calculator_calc_sub: no impl on this class");
        return ((calculator_calc_sub_fn)fn)(ctx, obj, x, y);
    }
}

struct yaafc_int64_result calculator_calc_mul(struct ctx * ctx, struct object * obj, int64_t x, int64_t y)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("calculator", (method_id_t)calculator_calc_mul);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int64, "calculator_calc_mul: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int64, "calculator_calc_mul: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int64, "calculator_calc_mul: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int64, "calculator_calc_mul: pack overflow");
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
                return YAAFC_ERR(yaafc_int64, "calculator_calc_mul: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(x) > sizeof(_a))
            return YAAFC_ERR(yaafc_int64, "calculator_calc_mul: pack overflow");
        memcpy(_a + _off, &x, sizeof(x)); _off += sizeof(x);
        if (_off + sizeof(y) > sizeof(_a))
            return YAAFC_ERR(yaafc_int64, "calculator_calc_mul: pack overflow");
        memcpy(_a + _off, &y, sizeof(y)); _off += sizeof(y);
        uint8_t _wbuf[1 + 4 + 256];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YAAFC_ERR(yaafc_int64, "calculator_calc_mul: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int64, _msg[0] ? strdup(_msg) : "calculator_calc_mul: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int64_t)) return YAAFC_ERR(yaafc_int64, "calculator_calc_mul: truncated RPC payload");
        int64_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int64, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int64, "calculator_calc_mul: no impl on this class");
        return ((calculator_calc_mul_fn)fn)(ctx, obj, x, y);
    }
}

struct yaafc_int64_result calculator_calc_div(struct ctx * ctx, struct object * obj, int64_t x, int64_t y)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("calculator", (method_id_t)calculator_calc_div);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int64, "calculator_calc_div: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int64, "calculator_calc_div: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int64, "calculator_calc_div: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int64, "calculator_calc_div: pack overflow");
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
                return YAAFC_ERR(yaafc_int64, "calculator_calc_div: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(x) > sizeof(_a))
            return YAAFC_ERR(yaafc_int64, "calculator_calc_div: pack overflow");
        memcpy(_a + _off, &x, sizeof(x)); _off += sizeof(x);
        if (_off + sizeof(y) > sizeof(_a))
            return YAAFC_ERR(yaafc_int64, "calculator_calc_div: pack overflow");
        memcpy(_a + _off, &y, sizeof(y)); _off += sizeof(y);
        uint8_t _wbuf[1 + 4 + 256];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YAAFC_ERR(yaafc_int64, "calculator_calc_div: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int64, _msg[0] ? strdup(_msg) : "calculator_calc_div: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int64_t)) return YAAFC_ERR(yaafc_int64, "calculator_calc_div: truncated RPC payload");
        int64_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int64, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int64, "calculator_calc_div: no impl on this class");
        return ((calculator_calc_div_fn)fn)(ctx, obj, x, y);
    }
}

