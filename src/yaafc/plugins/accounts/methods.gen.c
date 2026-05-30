/* GENERATED — do not edit. */
#include "accounts.internal.h"
#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/ycore/yspan.h>
#include <yaafc/yclass/rpc.h>
#include <yaafc/yclass/yheaders.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct yaafc_int_result accounts_store_register(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t uid)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("accounts", (method_id_t)accounts_store_register);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "accounts_store_register: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "accounts_store_register: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "accounts_store_register: remote id unresolved");
        uint8_t _a[16384];
        size_t _off = 0;
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, sid, trace_id, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. The codegen never inspects the
         * contents — it just lets the framework (de)serialize the bag. */
        {
            size_t _hn = yheaders_serialize(hdrs, _a, sizeof(_a));
            if (_hn == 0)
                return YAAFC_ERR(yaafc_int, "accounts_store_register: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "accounts_store_register: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(uid) > sizeof(_a))
            return YAAFC_ERR(yaafc_int, "accounts_store_register: pack overflow");
        memcpy(_a + _off, &uid, sizeof(uid)); _off += sizeof(uid);
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.accounts_store_register dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.accounts_store_register", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "accounts_store_register: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "accounts_store_register: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "accounts_store_register: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "accounts_store_register: no impl on this class");
        return ((accounts_store_register_fn)fn)(ctx, obj, hdrs, uid);
    }
}

struct yaafc_int_result accounts_store_exists(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t uid)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("accounts", (method_id_t)accounts_store_exists);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "accounts_store_exists: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "accounts_store_exists: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "accounts_store_exists: remote id unresolved");
        uint8_t _a[16384];
        size_t _off = 0;
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, sid, trace_id, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. The codegen never inspects the
         * contents — it just lets the framework (de)serialize the bag. */
        {
            size_t _hn = yheaders_serialize(hdrs, _a, sizeof(_a));
            if (_hn == 0)
                return YAAFC_ERR(yaafc_int, "accounts_store_exists: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "accounts_store_exists: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(uid) > sizeof(_a))
            return YAAFC_ERR(yaafc_int, "accounts_store_exists: pack overflow");
        memcpy(_a + _off, &uid, sizeof(uid)); _off += sizeof(uid);
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.accounts_store_exists dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.accounts_store_exists", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "accounts_store_exists: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "accounts_store_exists: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "accounts_store_exists: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "accounts_store_exists: no impl on this class");
        return ((accounts_store_exists_fn)fn)(ctx, obj, hdrs, uid);
    }
}

struct yaafc_int_result accounts_store_set_balance(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t uid, int64_t n)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("accounts", (method_id_t)accounts_store_set_balance);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "accounts_store_set_balance: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "accounts_store_set_balance: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "accounts_store_set_balance: remote id unresolved");
        uint8_t _a[16384];
        size_t _off = 0;
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, sid, trace_id, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. The codegen never inspects the
         * contents — it just lets the framework (de)serialize the bag. */
        {
            size_t _hn = yheaders_serialize(hdrs, _a, sizeof(_a));
            if (_hn == 0)
                return YAAFC_ERR(yaafc_int, "accounts_store_set_balance: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "accounts_store_set_balance: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(uid) > sizeof(_a))
            return YAAFC_ERR(yaafc_int, "accounts_store_set_balance: pack overflow");
        memcpy(_a + _off, &uid, sizeof(uid)); _off += sizeof(uid);
        if (_off + sizeof(n) > sizeof(_a))
            return YAAFC_ERR(yaafc_int, "accounts_store_set_balance: pack overflow");
        memcpy(_a + _off, &n, sizeof(n)); _off += sizeof(n);
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.accounts_store_set_balance dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.accounts_store_set_balance", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "accounts_store_set_balance: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "accounts_store_set_balance: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "accounts_store_set_balance: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "accounts_store_set_balance: no impl on this class");
        return ((accounts_store_set_balance_fn)fn)(ctx, obj, hdrs, uid, n);
    }
}

struct yaafc_int64_result accounts_store_balance(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t uid)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("accounts", (method_id_t)accounts_store_balance);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int64, "accounts_store_balance: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int64, "accounts_store_balance: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int64, "accounts_store_balance: remote id unresolved");
        uint8_t _a[16384];
        size_t _off = 0;
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, sid, trace_id, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. The codegen never inspects the
         * contents — it just lets the framework (de)serialize the bag. */
        {
            size_t _hn = yheaders_serialize(hdrs, _a, sizeof(_a));
            if (_hn == 0)
                return YAAFC_ERR(yaafc_int64, "accounts_store_balance: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int64, "accounts_store_balance: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(uid) > sizeof(_a))
            return YAAFC_ERR(yaafc_int64, "accounts_store_balance: pack overflow");
        memcpy(_a + _off, &uid, sizeof(uid)); _off += sizeof(uid);
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.accounts_store_balance dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.accounts_store_balance", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_int64, "accounts_store_balance: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int64, _msg[0] ? strdup(_msg) : "accounts_store_balance: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int64_t)) return YAAFC_ERR(yaafc_int64, "accounts_store_balance: truncated RPC payload");
        int64_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int64, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int64, "accounts_store_balance: no impl on this class");
        return ((accounts_store_balance_fn)fn)(ctx, obj, hdrs, uid);
    }
}

struct yaafc_size_result accounts_store_count(struct ctx * ctx, struct object * obj, struct yheaders * hdrs)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("accounts", (method_id_t)accounts_store_count);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_size, "accounts_store_count: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_size, "accounts_store_count: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_size, "accounts_store_count: remote id unresolved");
        uint8_t _a[16384];
        size_t _off = 0;
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, sid, trace_id, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. The codegen never inspects the
         * contents — it just lets the framework (de)serialize the bag. */
        {
            size_t _hn = yheaders_serialize(hdrs, _a, sizeof(_a));
            if (_hn == 0)
                return YAAFC_ERR(yaafc_size, "accounts_store_count: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_size, "accounts_store_count: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.accounts_store_count dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.accounts_store_count", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_size, "accounts_store_count: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_size, _msg[0] ? strdup(_msg) : "accounts_store_count: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(size_t)) return YAAFC_ERR(yaafc_size, "accounts_store_count: truncated RPC payload");
        size_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_size, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_size, "accounts_store_count: no impl on this class");
        return ((accounts_store_count_fn)fn)(ctx, obj, hdrs);
    }
}

