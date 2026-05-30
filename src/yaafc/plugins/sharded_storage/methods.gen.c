/* GENERATED — do not edit. */
#include "sharded_storage.internal.h"
#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/ycore/yspan.h>
#include <yaafc/yclass/rpc.h>
#include <yaafc/yclass/yheaders.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct yaafc_int_result sharded_storage_db_set(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key, const char * value)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("sharded_storage", (method_id_t)sharded_storage_db_set);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "sharded_storage_db_set: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "sharded_storage_db_set: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "sharded_storage_db_set: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int, "sharded_storage_db_set: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "sharded_storage_db_set: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(context ? strlen(context) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "sharded_storage_db_set: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, context, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(key ? strlen(key) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "sharded_storage_db_set: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, key, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(value ? strlen(value) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "sharded_storage_db_set: pack overflow");
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
        ydebug("span trace=%s op=rpc.sharded_storage_db_set dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.sharded_storage_db_set", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "sharded_storage_db_set: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "sharded_storage_db_set: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "sharded_storage_db_set: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "sharded_storage_db_set: no impl on this class");
        return ((sharded_storage_db_set_fn)fn)(ctx, obj, hdrs, context, key, value);
    }
}

struct yaafc_string_result sharded_storage_db_get(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("sharded_storage", (method_id_t)sharded_storage_db_get);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_string, "sharded_storage_db_get: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_string, "sharded_storage_db_get: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_string, "sharded_storage_db_get: remote id unresolved");
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
                return YAAFC_ERR(yaafc_string, "sharded_storage_db_get: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_string, "sharded_storage_db_get: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(context ? strlen(context) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_string, "sharded_storage_db_get: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, context, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(key ? strlen(key) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_string, "sharded_storage_db_get: pack overflow");
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
        ydebug("span trace=%s op=rpc.sharded_storage_db_get dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.sharded_storage_db_get", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_string, "sharded_storage_db_get: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_string, _msg[0] ? strdup(_msg) : "sharded_storage_db_get: remote error (no msg)");
        }
        if (_wn < 5) return YAAFC_ERR(yaafc_string, "sharded_storage_db_get: truncated string response");
        uint32_t _slen;
        memcpy(&_slen, _wbuf + 1, 4);
        if (_wn < (size_t)5 + _slen) return YAAFC_ERR(yaafc_string, "sharded_storage_db_get: truncated string payload");
        char *_sv = malloc((size_t)_slen + 1);
        if (!_sv) return YAAFC_ERR(yaafc_string, "sharded_storage_db_get: out of memory");
        if (_slen) memcpy(_sv, _wbuf + 5, _slen);
        _sv[_slen] = 0;
        return YAAFC_OK(yaafc_string, _sv);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_string, "sharded_storage_db_get: no impl on this class");
        return ((sharded_storage_db_get_fn)fn)(ctx, obj, hdrs, context, key);
    }
}

struct yaafc_int_result sharded_storage_db_exists(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("sharded_storage", (method_id_t)sharded_storage_db_exists);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "sharded_storage_db_exists: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "sharded_storage_db_exists: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "sharded_storage_db_exists: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int, "sharded_storage_db_exists: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "sharded_storage_db_exists: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(context ? strlen(context) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "sharded_storage_db_exists: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, context, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(key ? strlen(key) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "sharded_storage_db_exists: pack overflow");
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
        ydebug("span trace=%s op=rpc.sharded_storage_db_exists dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.sharded_storage_db_exists", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "sharded_storage_db_exists: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "sharded_storage_db_exists: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "sharded_storage_db_exists: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "sharded_storage_db_exists: no impl on this class");
        return ((sharded_storage_db_exists_fn)fn)(ctx, obj, hdrs, context, key);
    }
}

struct yaafc_int_result sharded_storage_db_del(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("sharded_storage", (method_id_t)sharded_storage_db_del);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "sharded_storage_db_del: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "sharded_storage_db_del: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "sharded_storage_db_del: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int, "sharded_storage_db_del: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "sharded_storage_db_del: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(context ? strlen(context) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "sharded_storage_db_del: pack overflow");
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, context, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(key ? strlen(key) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "sharded_storage_db_del: pack overflow");
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
        ydebug("span trace=%s op=rpc.sharded_storage_db_del dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.sharded_storage_db_del", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "sharded_storage_db_del: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "sharded_storage_db_del: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "sharded_storage_db_del: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "sharded_storage_db_del: no impl on this class");
        return ((sharded_storage_db_del_fn)fn)(ctx, obj, hdrs, context, key);
    }
}

struct yaafc_size_result sharded_storage_db_count(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("sharded_storage", (method_id_t)sharded_storage_db_count);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_size, "sharded_storage_db_count: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_size, "sharded_storage_db_count: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_size, "sharded_storage_db_count: remote id unresolved");
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
                return YAAFC_ERR(yaafc_size, "sharded_storage_db_count: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_size, "sharded_storage_db_count: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(context ? strlen(context) : 0);
            if (_off + 4 + _slen > sizeof(_a))
                return YAAFC_ERR(yaafc_size, "sharded_storage_db_count: pack overflow");
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
        ydebug("span trace=%s op=rpc.sharded_storage_db_count dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.sharded_storage_db_count", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_size, "sharded_storage_db_count: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_size, _msg[0] ? strdup(_msg) : "sharded_storage_db_count: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(size_t)) return YAAFC_ERR(yaafc_size, "sharded_storage_db_count: truncated RPC payload");
        size_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_size, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_size, "sharded_storage_db_count: no impl on this class");
        return ((sharded_storage_db_count_fn)fn)(ctx, obj, hdrs, context);
    }
}

