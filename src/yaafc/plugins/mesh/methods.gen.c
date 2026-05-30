/* GENERATED — do not edit. */
#include "mesh.internal.h"
#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/ycore/yspan.h>
#include <yaafc/yclass/rpc.h>
#include <yaafc/yclass/yheaders.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct yaafc_int_result mesh_store_register_service(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t service_id, uint32_t port)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("mesh", (method_id_t)mesh_store_register_service);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "mesh_store_register_service: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "mesh_store_register_service: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "mesh_store_register_service: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int, "mesh_store_register_service: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "mesh_store_register_service: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(service_id) > sizeof(_a))
            return YAAFC_ERR(yaafc_int, "mesh_store_register_service: pack overflow");
        memcpy(_a + _off, &service_id, sizeof(service_id)); _off += sizeof(service_id);
        if (_off + sizeof(port) > sizeof(_a))
            return YAAFC_ERR(yaafc_int, "mesh_store_register_service: pack overflow");
        memcpy(_a + _off, &port, sizeof(port)); _off += sizeof(port);
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.mesh_store_register_service dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.mesh_store_register_service", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "mesh_store_register_service: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "mesh_store_register_service: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "mesh_store_register_service: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "mesh_store_register_service: no impl on this class");
        return ((mesh_store_register_service_fn)fn)(ctx, obj, hdrs, service_id, port);
    }
}

struct yaafc_uint32_result mesh_store_resolve(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t service_id)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("mesh", (method_id_t)mesh_store_resolve);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_uint32, "mesh_store_resolve: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_uint32, "mesh_store_resolve: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_uint32, "mesh_store_resolve: remote id unresolved");
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
                return YAAFC_ERR(yaafc_uint32, "mesh_store_resolve: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_uint32, "mesh_store_resolve: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(service_id) > sizeof(_a))
            return YAAFC_ERR(yaafc_uint32, "mesh_store_resolve: pack overflow");
        memcpy(_a + _off, &service_id, sizeof(service_id)); _off += sizeof(service_id);
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.mesh_store_resolve dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.mesh_store_resolve", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_uint32, "mesh_store_resolve: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_uint32, _msg[0] ? strdup(_msg) : "mesh_store_resolve: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(uint32_t)) return YAAFC_ERR(yaafc_uint32, "mesh_store_resolve: truncated RPC payload");
        uint32_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_uint32, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_uint32, "mesh_store_resolve: no impl on this class");
        return ((mesh_store_resolve_fn)fn)(ctx, obj, hdrs, service_id);
    }
}

struct yaafc_int_result mesh_store_forget(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t service_id)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("mesh", (method_id_t)mesh_store_forget);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "mesh_store_forget: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "mesh_store_forget: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "mesh_store_forget: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int, "mesh_store_forget: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "mesh_store_forget: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(service_id) > sizeof(_a))
            return YAAFC_ERR(yaafc_int, "mesh_store_forget: pack overflow");
        memcpy(_a + _off, &service_id, sizeof(service_id)); _off += sizeof(service_id);
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.mesh_store_forget dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.mesh_store_forget", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "mesh_store_forget: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "mesh_store_forget: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "mesh_store_forget: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "mesh_store_forget: no impl on this class");
        return ((mesh_store_forget_fn)fn)(ctx, obj, hdrs, service_id);
    }
}

struct yaafc_size_result mesh_store_count_services(struct ctx * ctx, struct object * obj, struct yheaders * hdrs)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("mesh", (method_id_t)mesh_store_count_services);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_size, "mesh_store_count_services: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_size, "mesh_store_count_services: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_size, "mesh_store_count_services: remote id unresolved");
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
                return YAAFC_ERR(yaafc_size, "mesh_store_count_services: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_size, "mesh_store_count_services: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.mesh_store_count_services dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.mesh_store_count_services", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_size, "mesh_store_count_services: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_size, _msg[0] ? strdup(_msg) : "mesh_store_count_services: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(size_t)) return YAAFC_ERR(yaafc_size, "mesh_store_count_services: truncated RPC payload");
        size_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_size, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_size, "mesh_store_count_services: no impl on this class");
        return ((mesh_store_count_services_fn)fn)(ctx, obj, hdrs);
    }
}

struct yaafc_int_result mesh_store_spawn_yaafc(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t port)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("mesh", (method_id_t)mesh_store_spawn_yaafc);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "mesh_store_spawn_yaafc: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "mesh_store_spawn_yaafc: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "mesh_store_spawn_yaafc: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int, "mesh_store_spawn_yaafc: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "mesh_store_spawn_yaafc: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(port) > sizeof(_a))
            return YAAFC_ERR(yaafc_int, "mesh_store_spawn_yaafc: pack overflow");
        memcpy(_a + _off, &port, sizeof(port)); _off += sizeof(port);
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.mesh_store_spawn_yaafc dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.mesh_store_spawn_yaafc", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "mesh_store_spawn_yaafc: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "mesh_store_spawn_yaafc: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "mesh_store_spawn_yaafc: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "mesh_store_spawn_yaafc: no impl on this class");
        return ((mesh_store_spawn_yaafc_fn)fn)(ctx, obj, hdrs, port);
    }
}

struct yaafc_int_result mesh_store_kill_pid(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, int32_t pid)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("mesh", (method_id_t)mesh_store_kill_pid);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "mesh_store_kill_pid: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "mesh_store_kill_pid: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "mesh_store_kill_pid: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int, "mesh_store_kill_pid: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "mesh_store_kill_pid: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(pid) > sizeof(_a))
            return YAAFC_ERR(yaafc_int, "mesh_store_kill_pid: pack overflow");
        memcpy(_a + _off, &pid, sizeof(pid)); _off += sizeof(pid);
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.mesh_store_kill_pid dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.mesh_store_kill_pid", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "mesh_store_kill_pid: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "mesh_store_kill_pid: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "mesh_store_kill_pid: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "mesh_store_kill_pid: no impl on this class");
        return ((mesh_store_kill_pid_fn)fn)(ctx, obj, hdrs, pid);
    }
}

struct yaafc_size_result mesh_store_count_children(struct ctx * ctx, struct object * obj, struct yheaders * hdrs)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("mesh", (method_id_t)mesh_store_count_children);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_size, "mesh_store_count_children: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_size, "mesh_store_count_children: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_size, "mesh_store_count_children: remote id unresolved");
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
                return YAAFC_ERR(yaafc_size, "mesh_store_count_children: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_size, "mesh_store_count_children: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.mesh_store_count_children dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.mesh_store_count_children", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_size, "mesh_store_count_children: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_size, _msg[0] ? strdup(_msg) : "mesh_store_count_children: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(size_t)) return YAAFC_ERR(yaafc_size, "mesh_store_count_children: truncated RPC payload");
        size_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_size, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_size, "mesh_store_count_children: no impl on this class");
        return ((mesh_store_count_children_fn)fn)(ctx, obj, hdrs);
    }
}

struct yaafc_int_result mesh_store_reconcile_from_config(struct ctx * ctx, struct object * obj, struct yheaders * hdrs)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("mesh", (method_id_t)mesh_store_reconcile_from_config);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "mesh_store_reconcile_from_config: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "mesh_store_reconcile_from_config: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "mesh_store_reconcile_from_config: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int, "mesh_store_reconcile_from_config: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "mesh_store_reconcile_from_config: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.mesh_store_reconcile_from_config dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.mesh_store_reconcile_from_config", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "mesh_store_reconcile_from_config: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "mesh_store_reconcile_from_config: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "mesh_store_reconcile_from_config: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "mesh_store_reconcile_from_config: no impl on this class");
        return ((mesh_store_reconcile_from_config_fn)fn)(ctx, obj, hdrs);
    }
}

struct yaafc_int_result mesh_store_reconcile(struct ctx * ctx, struct object * obj, struct yheaders * hdrs)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("mesh", (method_id_t)mesh_store_reconcile);
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR(yaafc_int, "mesh_store_reconcile: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YAAFC_ERR(yaafc_int, "mesh_store_reconcile: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR(yaafc_int, "mesh_store_reconcile: remote id unresolved");
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
                return YAAFC_ERR(yaafc_int, "mesh_store_reconcile: header serialize overflow");
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > sizeof(_a))
                return YAAFC_ERR(yaafc_int, "mesh_store_reconcile: pack overflow");
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        const char *span_trace = hdrs ? yheaders_get(hdrs, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[261];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.mesh_store_reconcile dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.mesh_store_reconcile", span_us);
        if (_wn < 1) return YAAFC_ERR(yaafc_int, "mesh_store_reconcile: short RPC response");
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR(yaafc_int, _msg[0] ? strdup(_msg) : "mesh_store_reconcile: remote error (no msg)");
        }
        if (_wn != 1 + sizeof(int)) return YAAFC_ERR(yaafc_int, "mesh_store_reconcile: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK(yaafc_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YAAFC_ERR(yaafc_int, "mesh_store_reconcile: no impl on this class");
        return ((mesh_store_reconcile_fn)fn)(ctx, obj, hdrs);
    }
}

