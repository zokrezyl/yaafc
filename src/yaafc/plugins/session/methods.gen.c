/* GENERATED — do not edit. */
#include "session.internal.h"
#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/ycore/yspan.h>
#include <yaafc/yclass/rpc.h>
#include <yaafc/yclass/yheaders.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct yaafc_uint32_result session_store_start(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t user_id, uint32_t provider_id)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("session", (method_id_t)session_store_start);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_uint32, "session_store_start: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_uint32, "session_store_start: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_uint32, "session_store_start: remote id unresolved");
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
                return YAAFC_ERR(yaafc_uint32, "session_store_start: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_uint32, "session_store_start: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(user_id) > sizeof(_a))
            return YAAFC_ERR(yaafc_uint32, "session_store_start: pack overflow");
        memcpy(_a + _off, &user_id, sizeof(user_id)); _off += sizeof(user_id);
        if (_off + sizeof(provider_id) > sizeof(_a))
            return YAAFC_ERR(yaafc_uint32, "session_store_start: pack overflow");
        memcpy(_a + _off, &provider_id, sizeof(provider_id)); _off += sizeof(provider_id);
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.session_store_start dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.session_store_start", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_uint32, "session_store_start: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_uint32, _msg[0] ? strdup(_msg) : "session_store_start: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(uint32_t)) return YAAFC_ERR(yaafc_uint32, "session_store_start: truncated RPC payload");
        uint32_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_uint32, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_uint32, "session_store_start: no impl on this class");
        return ((session_store_start_fn)fn)(ctx, obj, hdrs, user_id, provider_id);
    }
}

struct yaafc_uint32_result session_store_lookup(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t sid)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("session", (method_id_t)session_store_lookup);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_uint32, "session_store_lookup: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_uint32, "session_store_lookup: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_uint32, "session_store_lookup: remote id unresolved");
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
                return YAAFC_ERR(yaafc_uint32, "session_store_lookup: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_uint32, "session_store_lookup: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(sid) > sizeof(_a))
            return YAAFC_ERR(yaafc_uint32, "session_store_lookup: pack overflow");
        memcpy(_a + _off, &sid, sizeof(sid)); _off += sizeof(sid);
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.session_store_lookup dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.session_store_lookup", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_uint32, "session_store_lookup: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_uint32, _msg[0] ? strdup(_msg) : "session_store_lookup: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(uint32_t)) return YAAFC_ERR(yaafc_uint32, "session_store_lookup: truncated RPC payload");
        uint32_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_uint32, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_uint32, "session_store_lookup: no impl on this class");
        return ((session_store_lookup_fn)fn)(ctx, obj, hdrs, sid);
    }
}

struct yaafc_int_result session_store_destroy(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t sid)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("session", (method_id_t)session_store_destroy);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "session_store_destroy: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "session_store_destroy: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "session_store_destroy: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int, "session_store_destroy: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "session_store_destroy: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(sid) > sizeof(_a))
            return YAAFC_ERR(yaafc_int, "session_store_destroy: pack overflow");
        memcpy(_a + _off, &sid, sizeof(sid)); _off += sizeof(sid);
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.session_store_destroy dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.session_store_destroy", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "session_store_destroy: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "session_store_destroy: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "session_store_destroy: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "session_store_destroy: no impl on this class");
        return ((session_store_destroy_fn)fn)(ctx, obj, hdrs, sid);
    }
}

struct yaafc_size_result session_store_count_active(struct ctx * ctx, struct object * obj, struct yheaders * hdrs)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("session", (method_id_t)session_store_count_active);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_size, "session_store_count_active: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_size, "session_store_count_active: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_size, "session_store_count_active: remote id unresolved");
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
                return YAAFC_ERR(yaafc_size, "session_store_count_active: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_size, "session_store_count_active: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.session_store_count_active dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.session_store_count_active", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_size, "session_store_count_active: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_size, _msg[0] ? strdup(_msg) : "session_store_count_active: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(size_t)) return YAAFC_ERR(yaafc_size, "session_store_count_active: truncated RPC payload");
        size_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_size, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_size, "session_store_count_active: no impl on this class");
        return ((session_store_count_active_fn)fn)(ctx, obj, hdrs);
    }
}

