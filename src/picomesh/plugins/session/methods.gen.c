/* GENERATED — do not edit. */
#include "session.internal.h"
#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>
#include <picomesh/core/yspan.h>
#include <picomesh/core/ytelemetry.h>
#include <picomesh/picoclass/rpc.h>
#include <picomesh/picoclass/yheaders.h>
#include <picomesh/msgpack/msgpack.h>
#include <picomesh/allocator/allocator.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct picomesh_string_result session_session_start(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t user_id, const char * access_jwt, const char * refresh_token)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("session", (method_id_t)session_session_start);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_string, "session_session_start: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_string, "session_session_start: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_string_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(65539);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_string, "session_session_start: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 3u);
            cmp_write_uinteger(&_maw, (uint64_t)user_id);
            cmp_write_str(&_maw, access_jwt ? access_jwt : "", (uint32_t)(access_jwt ? strlen(access_jwt) : 0));
            cmp_write_str(&_maw, refresh_token ? refresh_token : "", (uint32_t)(refresh_token ? strlen(refresh_token) : 0));
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "session.session.start", hdrs,
                                           _margs, _mab.offset, _mresp, 65539,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_string, strdup(_merr))
                            : PICOMESH_ERR(picomesh_string, "session_session_start: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint32_t _mssz = 0;
            if (!cmp_read_str_size(&_mrr, &_mssz)) {
                _mret = PICOMESH_ERR(picomesh_string, "session_session_start: bad msgpack string result");
            } else if (_mrb.offset + _mssz > _mrlen) {
                _mret = PICOMESH_ERR(picomesh_string, "session_session_start: truncated msgpack string");
            } else {
                char *_msv = malloc((size_t)_mssz + 1);
                if (!_msv) _mret = PICOMESH_ERR(picomesh_string, "session_session_start: out of memory");
                else {
                    if (_mssz) memcpy(_msv, _mresp + _mrb.offset, _mssz);
                    _msv[_mssz] = 0;
                    _mret = PICOMESH_OK(picomesh_string, _msv);
                }
            }
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_string, "session_session_start: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_string, "session_session_start: remote id unresolved");
        /* Wire scratch comes from THIS THREAD's pool, not the stack: the arg
         * buffer (_a, _acap bytes) and response buffer (_wbuf) are large
         * (~16 KiB + up to 64 KiB), and a nested chain of in-process hops would
         * overflow the fixed-size coroutine stack if these were locals. Every
         * exit below routes through _rpc_done, which returns both to the pool
         * exactly once (free(NULL) is a no-op). */
        struct picomesh_allocator *_pool = picomesh_allocator_thread();
        size_t _acap = 16384;
        struct picomesh_string_result _ret;
        uint8_t *_a = (uint8_t *)picomesh_allocator_alloc(_pool, _acap);
        uint8_t *_wbuf = (uint8_t *)picomesh_allocator_alloc(_pool, 65536);
        if (!_a || !_wbuf) {
            picomesh_allocator_free(_pool, _a);
            picomesh_allocator_free(_pool, _wbuf);
            return PICOMESH_ERR(picomesh_string, "session_session_start: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.session_session_start");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "session_session_start: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_string, "session_session_start: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "session_session_start: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "session_session_start: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(user_id) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "session_session_start: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "session_session_start: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &user_id, sizeof(user_id)); _off += sizeof(user_id);
        {
            uint32_t _slen = (uint32_t)(access_jwt ? strlen(access_jwt) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "session_session_start: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "session_session_start: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, access_jwt, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(refresh_token ? strlen(refresh_token) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "session_session_start: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "session_session_start: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, refresh_token, _slen); _off += _slen; }
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 65536);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_string, "session_session_start: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_string, strdup(_msg))
                       : PICOMESH_ERR(picomesh_string, "session_session_start: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn < 5) { _ret = PICOMESH_ERR(picomesh_string, "session_session_start: truncated string response"); goto _rpc_done; }
        uint32_t _slen;
        memcpy(&_slen, _wbuf + 1, 4);
        if (_wn < (size_t)5 + _slen) { _ret = PICOMESH_ERR(picomesh_string, "session_session_start: truncated string payload"); goto _rpc_done; }
        char *_sv = malloc((size_t)_slen + 1);
        if (!_sv) { _ret = PICOMESH_ERR(picomesh_string, "session_session_start: out of memory"); goto _rpc_done; }
        if (_slen) memcpy(_sv, _wbuf + 5, _slen);
        _sv[_slen] = 0;
        _ret = PICOMESH_OK(picomesh_string, _sv); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_string, "session_session_start: no impl on this class");
        return ((session_session_start_fn)fn)(ctx, obj, hdrs, user_id, access_jwt, refresh_token);
    }
}

struct picomesh_string_result session_session_jwt(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * sid)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("session", (method_id_t)session_session_jwt);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_string, "session_session_jwt: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_string, "session_session_jwt: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_string_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(65539);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_string, "session_session_jwt: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 1u);
            cmp_write_str(&_maw, sid ? sid : "", (uint32_t)(sid ? strlen(sid) : 0));
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "session.session.jwt", hdrs,
                                           _margs, _mab.offset, _mresp, 65539,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_string, strdup(_merr))
                            : PICOMESH_ERR(picomesh_string, "session_session_jwt: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint32_t _mssz = 0;
            if (!cmp_read_str_size(&_mrr, &_mssz)) {
                _mret = PICOMESH_ERR(picomesh_string, "session_session_jwt: bad msgpack string result");
            } else if (_mrb.offset + _mssz > _mrlen) {
                _mret = PICOMESH_ERR(picomesh_string, "session_session_jwt: truncated msgpack string");
            } else {
                char *_msv = malloc((size_t)_mssz + 1);
                if (!_msv) _mret = PICOMESH_ERR(picomesh_string, "session_session_jwt: out of memory");
                else {
                    if (_mssz) memcpy(_msv, _mresp + _mrb.offset, _mssz);
                    _msv[_mssz] = 0;
                    _mret = PICOMESH_OK(picomesh_string, _msv);
                }
            }
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_string, "session_session_jwt: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_string, "session_session_jwt: remote id unresolved");
        /* Wire scratch comes from THIS THREAD's pool, not the stack: the arg
         * buffer (_a, _acap bytes) and response buffer (_wbuf) are large
         * (~16 KiB + up to 64 KiB), and a nested chain of in-process hops would
         * overflow the fixed-size coroutine stack if these were locals. Every
         * exit below routes through _rpc_done, which returns both to the pool
         * exactly once (free(NULL) is a no-op). */
        struct picomesh_allocator *_pool = picomesh_allocator_thread();
        size_t _acap = 16384;
        struct picomesh_string_result _ret;
        uint8_t *_a = (uint8_t *)picomesh_allocator_alloc(_pool, _acap);
        uint8_t *_wbuf = (uint8_t *)picomesh_allocator_alloc(_pool, 65536);
        if (!_a || !_wbuf) {
            picomesh_allocator_free(_pool, _a);
            picomesh_allocator_free(_pool, _wbuf);
            return PICOMESH_ERR(picomesh_string, "session_session_jwt: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.session_session_jwt");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "session_session_jwt: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_string, "session_session_jwt: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "session_session_jwt: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "session_session_jwt: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(sid ? strlen(sid) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "session_session_jwt: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "session_session_jwt: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, sid, _slen); _off += _slen; }
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 65536);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_string, "session_session_jwt: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_string, strdup(_msg))
                       : PICOMESH_ERR(picomesh_string, "session_session_jwt: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn < 5) { _ret = PICOMESH_ERR(picomesh_string, "session_session_jwt: truncated string response"); goto _rpc_done; }
        uint32_t _slen;
        memcpy(&_slen, _wbuf + 1, 4);
        if (_wn < (size_t)5 + _slen) { _ret = PICOMESH_ERR(picomesh_string, "session_session_jwt: truncated string payload"); goto _rpc_done; }
        char *_sv = malloc((size_t)_slen + 1);
        if (!_sv) { _ret = PICOMESH_ERR(picomesh_string, "session_session_jwt: out of memory"); goto _rpc_done; }
        if (_slen) memcpy(_sv, _wbuf + 5, _slen);
        _sv[_slen] = 0;
        _ret = PICOMESH_OK(picomesh_string, _sv); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_string, "session_session_jwt: no impl on this class");
        return ((session_session_jwt_fn)fn)(ctx, obj, hdrs, sid);
    }
}

struct picomesh_uint32_result session_session_lookup(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * sid)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("session", (method_id_t)session_session_lookup);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_uint32, "session_session_lookup: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_uint32, "session_session_lookup: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_uint32_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_uint32, "session_session_lookup: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 1u);
            cmp_write_str(&_maw, sid ? sid : "", (uint32_t)(sid ? strlen(sid) : 0));
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "session.session.lookup", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_uint32, strdup(_merr))
                            : PICOMESH_ERR(picomesh_uint32, "session_session_lookup: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint64_t _mv = 0;
            if (!cmp_read_uinteger(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_uint32, "session_session_lookup: bad msgpack uint result");
            else _mret = PICOMESH_OK(picomesh_uint32, (uint32_t)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_uint32, "session_session_lookup: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_uint32, "session_session_lookup: remote id unresolved");
        /* Wire scratch comes from THIS THREAD's pool, not the stack: the arg
         * buffer (_a, _acap bytes) and response buffer (_wbuf) are large
         * (~16 KiB + up to 64 KiB), and a nested chain of in-process hops would
         * overflow the fixed-size coroutine stack if these were locals. Every
         * exit below routes through _rpc_done, which returns both to the pool
         * exactly once (free(NULL) is a no-op). */
        struct picomesh_allocator *_pool = picomesh_allocator_thread();
        size_t _acap = 16384;
        struct picomesh_uint32_result _ret;
        uint8_t *_a = (uint8_t *)picomesh_allocator_alloc(_pool, _acap);
        uint8_t *_wbuf = (uint8_t *)picomesh_allocator_alloc(_pool, 8197);
        if (!_a || !_wbuf) {
            picomesh_allocator_free(_pool, _a);
            picomesh_allocator_free(_pool, _wbuf);
            return PICOMESH_ERR(picomesh_uint32, "session_session_lookup: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.session_session_lookup");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "session_session_lookup: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_uint32, "session_session_lookup: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "session_session_lookup: pack overflow"); _ret = PICOMESH_ERR(picomesh_uint32, "session_session_lookup: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(sid ? strlen(sid) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "session_session_lookup: pack overflow"); _ret = PICOMESH_ERR(picomesh_uint32, "session_session_lookup: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, sid, _slen); _off += _slen; }
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_uint32, "session_session_lookup: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_uint32, strdup(_msg))
                       : PICOMESH_ERR(picomesh_uint32, "session_session_lookup: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(uint32_t)) { _ret = PICOMESH_ERR(picomesh_uint32, "session_session_lookup: truncated RPC payload"); goto _rpc_done; }
        uint32_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_uint32, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_uint32, "session_session_lookup: no impl on this class");
        return ((session_session_lookup_fn)fn)(ctx, obj, hdrs, sid);
    }
}

struct picomesh_int_result session_session_destroy(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * sid)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("session", (method_id_t)session_session_destroy);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_int, "session_session_destroy: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_int, "session_session_destroy: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_int_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_int, "session_session_destroy: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 1u);
            cmp_write_str(&_maw, sid ? sid : "", (uint32_t)(sid ? strlen(sid) : 0));
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "session.session.destroy", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_int, strdup(_merr))
                            : PICOMESH_ERR(picomesh_int, "session_session_destroy: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            int64_t _mv = 0;
            if (!cmp_read_integer(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_int, "session_session_destroy: bad msgpack int result");
            else _mret = PICOMESH_OK(picomesh_int, (int)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_int, "session_session_destroy: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_int, "session_session_destroy: remote id unresolved");
        /* Wire scratch comes from THIS THREAD's pool, not the stack: the arg
         * buffer (_a, _acap bytes) and response buffer (_wbuf) are large
         * (~16 KiB + up to 64 KiB), and a nested chain of in-process hops would
         * overflow the fixed-size coroutine stack if these were locals. Every
         * exit below routes through _rpc_done, which returns both to the pool
         * exactly once (free(NULL) is a no-op). */
        struct picomesh_allocator *_pool = picomesh_allocator_thread();
        size_t _acap = 16384;
        struct picomesh_int_result _ret;
        uint8_t *_a = (uint8_t *)picomesh_allocator_alloc(_pool, _acap);
        uint8_t *_wbuf = (uint8_t *)picomesh_allocator_alloc(_pool, 8197);
        if (!_a || !_wbuf) {
            picomesh_allocator_free(_pool, _a);
            picomesh_allocator_free(_pool, _wbuf);
            return PICOMESH_ERR(picomesh_int, "session_session_destroy: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.session_session_destroy");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "session_session_destroy: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_int, "session_session_destroy: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "session_session_destroy: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "session_session_destroy: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(sid ? strlen(sid) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "session_session_destroy: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "session_session_destroy: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, sid, _slen); _off += _slen; }
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_int, "session_session_destroy: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_int, strdup(_msg))
                       : PICOMESH_ERR(picomesh_int, "session_session_destroy: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(int)) { _ret = PICOMESH_ERR(picomesh_int, "session_session_destroy: truncated RPC payload"); goto _rpc_done; }
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_int, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_int, "session_session_destroy: no impl on this class");
        return ((session_session_destroy_fn)fn)(ctx, obj, hdrs, sid);
    }
}

struct picomesh_size_result session_session_count_active(struct ctx * ctx, struct object * obj, struct yheaders * hdrs)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("session", (method_id_t)session_session_count_active);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_size, "session_session_count_active: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_size, "session_session_count_active: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_size_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_size, "session_session_count_active: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 0u);
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "session.session.count_active", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_size, strdup(_merr))
                            : PICOMESH_ERR(picomesh_size, "session_session_count_active: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint64_t _mv = 0;
            if (!cmp_read_uinteger(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_size, "session_session_count_active: bad msgpack uint result");
            else _mret = PICOMESH_OK(picomesh_size, (size_t)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_size, "session_session_count_active: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_size, "session_session_count_active: remote id unresolved");
        /* Wire scratch comes from THIS THREAD's pool, not the stack: the arg
         * buffer (_a, _acap bytes) and response buffer (_wbuf) are large
         * (~16 KiB + up to 64 KiB), and a nested chain of in-process hops would
         * overflow the fixed-size coroutine stack if these were locals. Every
         * exit below routes through _rpc_done, which returns both to the pool
         * exactly once (free(NULL) is a no-op). */
        struct picomesh_allocator *_pool = picomesh_allocator_thread();
        size_t _acap = 16384;
        struct picomesh_size_result _ret;
        uint8_t *_a = (uint8_t *)picomesh_allocator_alloc(_pool, _acap);
        uint8_t *_wbuf = (uint8_t *)picomesh_allocator_alloc(_pool, 8197);
        if (!_a || !_wbuf) {
            picomesh_allocator_free(_pool, _a);
            picomesh_allocator_free(_pool, _wbuf);
            return PICOMESH_ERR(picomesh_size, "session_session_count_active: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.session_session_count_active");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "session_session_count_active: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_size, "session_session_count_active: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "session_session_count_active: pack overflow"); _ret = PICOMESH_ERR(picomesh_size, "session_session_count_active: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_size, "session_session_count_active: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_size, strdup(_msg))
                       : PICOMESH_ERR(picomesh_size, "session_session_count_active: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(size_t)) { _ret = PICOMESH_ERR(picomesh_size, "session_session_count_active: truncated RPC payload"); goto _rpc_done; }
        size_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_size, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_size, "session_session_count_active: no impl on this class");
        return ((session_session_count_active_fn)fn)(ctx, obj, hdrs);
    }
}

struct picomesh_json_result session_session_list(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, int64_t offset, int64_t limit)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("session", (method_id_t)session_session_list);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_json, "session_session_list: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_json, "session_session_list: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_json_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(65539);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_json, "session_session_list: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 2u);
            cmp_write_integer(&_maw, (int64_t)offset);
            cmp_write_integer(&_maw, (int64_t)limit);
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "session.session.list", hdrs,
                                           _margs, _mab.offset, _mresp, 65539,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_json, strdup(_merr))
                            : PICOMESH_ERR(picomesh_json, "session_session_list: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint32_t _mssz = 0;
            if (!cmp_read_str_size(&_mrr, &_mssz)) {
                _mret = PICOMESH_ERR(picomesh_json, "session_session_list: bad msgpack string result");
            } else if (_mrb.offset + _mssz > _mrlen) {
                _mret = PICOMESH_ERR(picomesh_json, "session_session_list: truncated msgpack string");
            } else {
                char *_msv = malloc((size_t)_mssz + 1);
                if (!_msv) _mret = PICOMESH_ERR(picomesh_json, "session_session_list: out of memory");
                else {
                    if (_mssz) memcpy(_msv, _mresp + _mrb.offset, _mssz);
                    _msv[_mssz] = 0;
                    _mret = PICOMESH_OK(picomesh_json, _msv);
                }
            }
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_json, "session_session_list: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_json, "session_session_list: remote id unresolved");
        /* Wire scratch comes from THIS THREAD's pool, not the stack: the arg
         * buffer (_a, _acap bytes) and response buffer (_wbuf) are large
         * (~16 KiB + up to 64 KiB), and a nested chain of in-process hops would
         * overflow the fixed-size coroutine stack if these were locals. Every
         * exit below routes through _rpc_done, which returns both to the pool
         * exactly once (free(NULL) is a no-op). */
        struct picomesh_allocator *_pool = picomesh_allocator_thread();
        size_t _acap = 16384;
        struct picomesh_json_result _ret;
        uint8_t *_a = (uint8_t *)picomesh_allocator_alloc(_pool, _acap);
        uint8_t *_wbuf = (uint8_t *)picomesh_allocator_alloc(_pool, 65536);
        if (!_a || !_wbuf) {
            picomesh_allocator_free(_pool, _a);
            picomesh_allocator_free(_pool, _wbuf);
            return PICOMESH_ERR(picomesh_json, "session_session_list: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.session_session_list");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "session_session_list: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_json, "session_session_list: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "session_session_list: pack overflow"); _ret = PICOMESH_ERR(picomesh_json, "session_session_list: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(offset) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "session_session_list: pack overflow"); _ret = PICOMESH_ERR(picomesh_json, "session_session_list: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &offset, sizeof(offset)); _off += sizeof(offset);
        if (_off + sizeof(limit) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "session_session_list: pack overflow"); _ret = PICOMESH_ERR(picomesh_json, "session_session_list: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &limit, sizeof(limit)); _off += sizeof(limit);
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 65536);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_json, "session_session_list: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_json, strdup(_msg))
                       : PICOMESH_ERR(picomesh_json, "session_session_list: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn < 5) { _ret = PICOMESH_ERR(picomesh_json, "session_session_list: truncated string response"); goto _rpc_done; }
        uint32_t _slen;
        memcpy(&_slen, _wbuf + 1, 4);
        if (_wn < (size_t)5 + _slen) { _ret = PICOMESH_ERR(picomesh_json, "session_session_list: truncated string payload"); goto _rpc_done; }
        char *_sv = malloc((size_t)_slen + 1);
        if (!_sv) { _ret = PICOMESH_ERR(picomesh_json, "session_session_list: out of memory"); goto _rpc_done; }
        if (_slen) memcpy(_sv, _wbuf + 5, _slen);
        _sv[_slen] = 0;
        _ret = PICOMESH_OK(picomesh_json, _sv); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_json, "session_session_list: no impl on this class");
        return ((session_session_list_fn)fn)(ctx, obj, hdrs, offset, limit);
    }
}

struct picomesh_json_result session_session_list_all(struct ctx * ctx, struct object * obj, struct yheaders * hdrs)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("session", (method_id_t)session_session_list_all);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_json, "session_session_list_all: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_json, "session_session_list_all: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_json_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(65539);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_json, "session_session_list_all: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 0u);
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "session.session.list_all", hdrs,
                                           _margs, _mab.offset, _mresp, 65539,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_json, strdup(_merr))
                            : PICOMESH_ERR(picomesh_json, "session_session_list_all: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint32_t _mssz = 0;
            if (!cmp_read_str_size(&_mrr, &_mssz)) {
                _mret = PICOMESH_ERR(picomesh_json, "session_session_list_all: bad msgpack string result");
            } else if (_mrb.offset + _mssz > _mrlen) {
                _mret = PICOMESH_ERR(picomesh_json, "session_session_list_all: truncated msgpack string");
            } else {
                char *_msv = malloc((size_t)_mssz + 1);
                if (!_msv) _mret = PICOMESH_ERR(picomesh_json, "session_session_list_all: out of memory");
                else {
                    if (_mssz) memcpy(_msv, _mresp + _mrb.offset, _mssz);
                    _msv[_mssz] = 0;
                    _mret = PICOMESH_OK(picomesh_json, _msv);
                }
            }
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_json, "session_session_list_all: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_json, "session_session_list_all: remote id unresolved");
        /* Wire scratch comes from THIS THREAD's pool, not the stack: the arg
         * buffer (_a, _acap bytes) and response buffer (_wbuf) are large
         * (~16 KiB + up to 64 KiB), and a nested chain of in-process hops would
         * overflow the fixed-size coroutine stack if these were locals. Every
         * exit below routes through _rpc_done, which returns both to the pool
         * exactly once (free(NULL) is a no-op). */
        struct picomesh_allocator *_pool = picomesh_allocator_thread();
        size_t _acap = 16384;
        struct picomesh_json_result _ret;
        uint8_t *_a = (uint8_t *)picomesh_allocator_alloc(_pool, _acap);
        uint8_t *_wbuf = (uint8_t *)picomesh_allocator_alloc(_pool, 65536);
        if (!_a || !_wbuf) {
            picomesh_allocator_free(_pool, _a);
            picomesh_allocator_free(_pool, _wbuf);
            return PICOMESH_ERR(picomesh_json, "session_session_list_all: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.session_session_list_all");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "session_session_list_all: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_json, "session_session_list_all: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "session_session_list_all: pack overflow"); _ret = PICOMESH_ERR(picomesh_json, "session_session_list_all: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 65536);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_json, "session_session_list_all: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_json, strdup(_msg))
                       : PICOMESH_ERR(picomesh_json, "session_session_list_all: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn < 5) { _ret = PICOMESH_ERR(picomesh_json, "session_session_list_all: truncated string response"); goto _rpc_done; }
        uint32_t _slen;
        memcpy(&_slen, _wbuf + 1, 4);
        if (_wn < (size_t)5 + _slen) { _ret = PICOMESH_ERR(picomesh_json, "session_session_list_all: truncated string payload"); goto _rpc_done; }
        char *_sv = malloc((size_t)_slen + 1);
        if (!_sv) { _ret = PICOMESH_ERR(picomesh_json, "session_session_list_all: out of memory"); goto _rpc_done; }
        if (_slen) memcpy(_sv, _wbuf + 5, _slen);
        _sv[_slen] = 0;
        _ret = PICOMESH_OK(picomesh_json, _sv); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_json, "session_session_list_all: no impl on this class");
        return ((session_session_list_all_fn)fn)(ctx, obj, hdrs);
    }
}

