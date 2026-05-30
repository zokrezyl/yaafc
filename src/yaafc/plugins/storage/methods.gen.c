/* GENERATED — do not edit. */
#include "storage.internal.h"
#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/ycore/yspan.h>
#include <yaafc/yclass/rpc.h>
#include <yaafc/yclass/yheaders.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct yaafc_int_result storage_kv_set(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * key, const char * value)
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
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "storage_kv_set: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int, "storage_kv_set: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "storage_kv_set: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(key ? strlen(key) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "storage_kv_set: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, key, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(value ? strlen(value) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "storage_kv_set: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, value, _slen); _off += _slen; }
        }
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.storage_kv_set dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.storage_kv_set", span_us);
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
        return ((storage_kv_set_fn)fn)(ctx, obj, hdrs, key, value);
    }
}

struct yaafc_string_result storage_kv_get(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * key)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("storage", (method_id_t)storage_kv_get);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_string, "storage_kv_get: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_string, "storage_kv_get: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_string, "storage_kv_get: remote id unresolved");
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
                return YAAFC_ERR(yaafc_string, "storage_kv_get: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_string, "storage_kv_get: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(key ? strlen(key) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_string, "storage_kv_get: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, key, _slen); _off += _slen; }
        }
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[4101];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.storage_kv_get dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.storage_kv_get", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_string, "storage_kv_get: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_string, _msg[0] ? strdup(_msg) : "storage_kv_get: remote error (no msg)");
        }
        if (_wn < 5) return YAAFC_ERR(yaafc_string, "storage_kv_get: truncated string response");
        uint32_t _slen;
        memcpy(&_slen, _wbuf + 1, 4);
        if (_wn < (size_t)5 + _slen) return YAAFC_ERR(yaafc_string, "storage_kv_get: truncated string payload");
        char *_sv = malloc((size_t)_slen + 1);
        if (!_sv) return YAAFC_ERR(yaafc_string, "storage_kv_get: out of memory");
        if (_slen) memcpy(_sv, _wbuf + 5, _slen);
        _sv[_slen] = 0;
        return YAAFC_OK(yaafc_string, _sv);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_string, "storage_kv_get: no impl on this class");
        return ((storage_kv_get_fn)fn)(ctx, obj, hdrs, key);
    }
}

struct yaafc_size_result storage_kv_count(struct ctx * ctx, struct object * obj, struct yheaders * hdrs)
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
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_size, "storage_kv_count: remote id unresolved");
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
                return YAAFC_ERR(yaafc_size, "storage_kv_count: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_size, "storage_kv_count: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.storage_kv_count dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.storage_kv_count", span_us);
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
        return ((storage_kv_count_fn)fn)(ctx, obj, hdrs);
    }
}

struct yaafc_int_result storage_set(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key, const char * value)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("storage", (method_id_t)storage_set);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "storage_set: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "storage_set: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "storage_set: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int, "storage_set: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "storage_set: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(context ? strlen(context) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "storage_set: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, context, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(key ? strlen(key) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "storage_set: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, key, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(value ? strlen(value) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "storage_set: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, value, _slen); _off += _slen; }
        }
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.storage_set dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.storage_set", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "storage_set: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "storage_set: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "storage_set: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "storage_set: no impl on this class");
        return ((storage_set_fn)fn)(ctx, obj, hdrs, context, key, value);
    }
}

struct yaafc_string_result storage_get(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("storage", (method_id_t)storage_get);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_string, "storage_get: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_string, "storage_get: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_string, "storage_get: remote id unresolved");
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
                return YAAFC_ERR(yaafc_string, "storage_get: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_string, "storage_get: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(context ? strlen(context) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_string, "storage_get: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, context, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(key ? strlen(key) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_string, "storage_get: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, key, _slen); _off += _slen; }
        }
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[4101];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.storage_get dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.storage_get", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_string, "storage_get: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_string, _msg[0] ? strdup(_msg) : "storage_get: remote error (no msg)");
        }
        if (_wn < 5) return YAAFC_ERR(yaafc_string, "storage_get: truncated string response");
        uint32_t _slen;
        memcpy(&_slen, _wbuf + 1, 4);
        if (_wn < (size_t)5 + _slen) return YAAFC_ERR(yaafc_string, "storage_get: truncated string payload");
        char *_sv = malloc((size_t)_slen + 1);
        if (!_sv) return YAAFC_ERR(yaafc_string, "storage_get: out of memory");
        if (_slen) memcpy(_sv, _wbuf + 5, _slen);
        _sv[_slen] = 0;
        return YAAFC_OK(yaafc_string, _sv);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_string, "storage_get: no impl on this class");
        return ((storage_get_fn)fn)(ctx, obj, hdrs, context, key);
    }
}

struct yaafc_int_result storage_exists(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("storage", (method_id_t)storage_exists);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "storage_exists: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "storage_exists: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "storage_exists: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int, "storage_exists: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "storage_exists: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(context ? strlen(context) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "storage_exists: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, context, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(key ? strlen(key) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "storage_exists: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, key, _slen); _off += _slen; }
        }
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.storage_exists dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.storage_exists", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "storage_exists: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "storage_exists: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "storage_exists: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "storage_exists: no impl on this class");
        return ((storage_exists_fn)fn)(ctx, obj, hdrs, context, key);
    }
}

struct yaafc_int_result storage_del(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("storage", (method_id_t)storage_del);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "storage_del: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "storage_del: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "storage_del: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int, "storage_del: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "storage_del: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(context ? strlen(context) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "storage_del: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, context, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(key ? strlen(key) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "storage_del: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, key, _slen); _off += _slen; }
        }
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.storage_del dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.storage_del", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "storage_del: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "storage_del: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "storage_del: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "storage_del: no impl on this class");
        return ((storage_del_fn)fn)(ctx, obj, hdrs, context, key);
    }
}

struct yaafc_size_result storage_count(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("storage", (method_id_t)storage_count);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_size, "storage_count: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_size, "storage_count: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_size, "storage_count: remote id unresolved");
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
                return YAAFC_ERR(yaafc_size, "storage_count: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_size, "storage_count: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(context ? strlen(context) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_size, "storage_count: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, context, _slen); _off += _slen; }
        }
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.storage_count dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.storage_count", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_size, "storage_count: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_size, _msg[0] ? strdup(_msg) : "storage_count: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(size_t)) return YAAFC_ERR(yaafc_size, "storage_count: truncated RPC payload");
        size_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_size, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_size, "storage_count: no impl on this class");
        return ((storage_count_fn)fn)(ctx, obj, hdrs, context);
    }
}

