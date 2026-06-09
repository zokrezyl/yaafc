/* GENERATED — do not edit. */
#include "sharded_storage.internal.h"
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

struct picomesh_int_result sharded_storage_db_set(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key, const char * value)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("sharded_storage", (method_id_t)sharded_storage_db_set);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_int, "sharded_storage_db_set: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_int, "sharded_storage_db_set: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_int_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_int, "sharded_storage_db_set: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 3u);
            cmp_write_str(&_maw, context ? context : "", (uint32_t)(context ? strlen(context) : 0));
            cmp_write_str(&_maw, key ? key : "", (uint32_t)(key ? strlen(key) : 0));
            cmp_write_str(&_maw, value ? value : "", (uint32_t)(value ? strlen(value) : 0));
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "sharded_storage.db.set", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_int, strdup(_merr))
                            : PICOMESH_ERR(picomesh_int, "sharded_storage_db_set: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            int64_t _mv = 0;
            if (!cmp_read_integer(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_set: bad msgpack int result");
            else _mret = PICOMESH_OK(picomesh_int, (int)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_int, "sharded_storage_db_set: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_int, "sharded_storage_db_set: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_int, "sharded_storage_db_set: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.sharded_storage_db_set");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_set: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_set: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_set: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_set: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(context ? strlen(context) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_set: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_set: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, context, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(key ? strlen(key) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_set: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_set: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, key, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(value ? strlen(value) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_set: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_set: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, value, _slen); _off += _slen; }
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_set: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_int, strdup(_msg))
                       : PICOMESH_ERR(picomesh_int, "sharded_storage_db_set: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(int)) { _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_set: truncated RPC payload"); goto _rpc_done; }
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_int, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_int, "sharded_storage_db_set: no impl on this class");
        return ((sharded_storage_db_set_fn)fn)(ctx, obj, hdrs, context, key, value);
    }
}

struct picomesh_string_result sharded_storage_db_get(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("sharded_storage", (method_id_t)sharded_storage_db_get);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_string, "sharded_storage_db_get: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_string, "sharded_storage_db_get: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_string_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(65539);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_string, "sharded_storage_db_get: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 2u);
            cmp_write_str(&_maw, context ? context : "", (uint32_t)(context ? strlen(context) : 0));
            cmp_write_str(&_maw, key ? key : "", (uint32_t)(key ? strlen(key) : 0));
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "sharded_storage.db.get", hdrs,
                                           _margs, _mab.offset, _mresp, 65539,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_string, strdup(_merr))
                            : PICOMESH_ERR(picomesh_string, "sharded_storage_db_get: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint32_t _mssz = 0;
            if (!cmp_read_str_size(&_mrr, &_mssz)) {
                _mret = PICOMESH_ERR(picomesh_string, "sharded_storage_db_get: bad msgpack string result");
            } else if (_mrb.offset + _mssz > _mrlen) {
                _mret = PICOMESH_ERR(picomesh_string, "sharded_storage_db_get: truncated msgpack string");
            } else {
                char *_msv = malloc((size_t)_mssz + 1);
                if (!_msv) _mret = PICOMESH_ERR(picomesh_string, "sharded_storage_db_get: out of memory");
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
            return PICOMESH_ERR(picomesh_string, "sharded_storage_db_get: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_string, "sharded_storage_db_get: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_string, "sharded_storage_db_get: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.sharded_storage_db_get");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_get: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_string, "sharded_storage_db_get: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_get: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "sharded_storage_db_get: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(context ? strlen(context) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_get: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "sharded_storage_db_get: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, context, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(key ? strlen(key) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_get: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "sharded_storage_db_get: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, key, _slen); _off += _slen; }
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 65536);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_string, "sharded_storage_db_get: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_string, strdup(_msg))
                       : PICOMESH_ERR(picomesh_string, "sharded_storage_db_get: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn < 5) { _ret = PICOMESH_ERR(picomesh_string, "sharded_storage_db_get: truncated string response"); goto _rpc_done; }
        uint32_t _slen;
        memcpy(&_slen, _wbuf + 1, 4);
        if (_wn < (size_t)5 + _slen) { _ret = PICOMESH_ERR(picomesh_string, "sharded_storage_db_get: truncated string payload"); goto _rpc_done; }
        char *_sv = malloc((size_t)_slen + 1);
        if (!_sv) { _ret = PICOMESH_ERR(picomesh_string, "sharded_storage_db_get: out of memory"); goto _rpc_done; }
        if (_slen) memcpy(_sv, _wbuf + 5, _slen);
        _sv[_slen] = 0;
        _ret = PICOMESH_OK(picomesh_string, _sv); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_string, "sharded_storage_db_get: no impl on this class");
        return ((sharded_storage_db_get_fn)fn)(ctx, obj, hdrs, context, key);
    }
}

struct picomesh_int_result sharded_storage_db_exists(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("sharded_storage", (method_id_t)sharded_storage_db_exists);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_int, "sharded_storage_db_exists: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_int, "sharded_storage_db_exists: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_int_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_int, "sharded_storage_db_exists: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 2u);
            cmp_write_str(&_maw, context ? context : "", (uint32_t)(context ? strlen(context) : 0));
            cmp_write_str(&_maw, key ? key : "", (uint32_t)(key ? strlen(key) : 0));
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "sharded_storage.db.exists", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_int, strdup(_merr))
                            : PICOMESH_ERR(picomesh_int, "sharded_storage_db_exists: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            int64_t _mv = 0;
            if (!cmp_read_integer(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_exists: bad msgpack int result");
            else _mret = PICOMESH_OK(picomesh_int, (int)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_int, "sharded_storage_db_exists: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_int, "sharded_storage_db_exists: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_int, "sharded_storage_db_exists: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.sharded_storage_db_exists");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_exists: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_exists: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_exists: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_exists: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(context ? strlen(context) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_exists: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_exists: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, context, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(key ? strlen(key) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_exists: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_exists: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, key, _slen); _off += _slen; }
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_exists: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_int, strdup(_msg))
                       : PICOMESH_ERR(picomesh_int, "sharded_storage_db_exists: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(int)) { _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_exists: truncated RPC payload"); goto _rpc_done; }
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_int, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_int, "sharded_storage_db_exists: no impl on this class");
        return ((sharded_storage_db_exists_fn)fn)(ctx, obj, hdrs, context, key);
    }
}

struct picomesh_int_result sharded_storage_db_del(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("sharded_storage", (method_id_t)sharded_storage_db_del);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_int, "sharded_storage_db_del: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_int, "sharded_storage_db_del: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_int_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_int, "sharded_storage_db_del: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 2u);
            cmp_write_str(&_maw, context ? context : "", (uint32_t)(context ? strlen(context) : 0));
            cmp_write_str(&_maw, key ? key : "", (uint32_t)(key ? strlen(key) : 0));
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "sharded_storage.db.del", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_int, strdup(_merr))
                            : PICOMESH_ERR(picomesh_int, "sharded_storage_db_del: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            int64_t _mv = 0;
            if (!cmp_read_integer(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_del: bad msgpack int result");
            else _mret = PICOMESH_OK(picomesh_int, (int)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_int, "sharded_storage_db_del: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_int, "sharded_storage_db_del: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_int, "sharded_storage_db_del: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.sharded_storage_db_del");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_del: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_del: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_del: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_del: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(context ? strlen(context) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_del: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_del: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, context, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(key ? strlen(key) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_del: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_del: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, key, _slen); _off += _slen; }
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_del: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_int, strdup(_msg))
                       : PICOMESH_ERR(picomesh_int, "sharded_storage_db_del: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(int)) { _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_del: truncated RPC payload"); goto _rpc_done; }
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_int, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_int, "sharded_storage_db_del: no impl on this class");
        return ((sharded_storage_db_del_fn)fn)(ctx, obj, hdrs, context, key);
    }
}

struct picomesh_size_result sharded_storage_db_count(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("sharded_storage", (method_id_t)sharded_storage_db_count);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_size, "sharded_storage_db_count: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_size, "sharded_storage_db_count: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_size_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_size, "sharded_storage_db_count: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 1u);
            cmp_write_str(&_maw, context ? context : "", (uint32_t)(context ? strlen(context) : 0));
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "sharded_storage.db.count", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_size, strdup(_merr))
                            : PICOMESH_ERR(picomesh_size, "sharded_storage_db_count: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint64_t _mv = 0;
            if (!cmp_read_uinteger(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_size, "sharded_storage_db_count: bad msgpack uint result");
            else _mret = PICOMESH_OK(picomesh_size, (size_t)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_size, "sharded_storage_db_count: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_size, "sharded_storage_db_count: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_size, "sharded_storage_db_count: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.sharded_storage_db_count");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_count: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_size, "sharded_storage_db_count: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_count: pack overflow"); _ret = PICOMESH_ERR(picomesh_size, "sharded_storage_db_count: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(context ? strlen(context) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_count: pack overflow"); _ret = PICOMESH_ERR(picomesh_size, "sharded_storage_db_count: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, context, _slen); _off += _slen; }
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_size, "sharded_storage_db_count: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_size, strdup(_msg))
                       : PICOMESH_ERR(picomesh_size, "sharded_storage_db_count: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(size_t)) { _ret = PICOMESH_ERR(picomesh_size, "sharded_storage_db_count: truncated RPC payload"); goto _rpc_done; }
        size_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_size, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_size, "sharded_storage_db_count: no impl on this class");
        return ((sharded_storage_db_count_fn)fn)(ctx, obj, hdrs, context);
    }
}

struct picomesh_json_result sharded_storage_db_list(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * prefix, int64_t offset, int64_t limit)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("sharded_storage", (method_id_t)sharded_storage_db_list);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_json_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(65539);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 4u);
            cmp_write_str(&_maw, context ? context : "", (uint32_t)(context ? strlen(context) : 0));
            cmp_write_str(&_maw, prefix ? prefix : "", (uint32_t)(prefix ? strlen(prefix) : 0));
            cmp_write_integer(&_maw, (int64_t)offset);
            cmp_write_integer(&_maw, (int64_t)limit);
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "sharded_storage.db.list", hdrs,
                                           _margs, _mab.offset, _mresp, 65539,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_json, strdup(_merr))
                            : PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint32_t _mssz = 0;
            if (!cmp_read_str_size(&_mrr, &_mssz)) {
                _mret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: bad msgpack string result");
            } else if (_mrb.offset + _mssz > _mrlen) {
                _mret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: truncated msgpack string");
            } else {
                char *_msv = malloc((size_t)_mssz + 1);
                if (!_msv) _mret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: out of memory");
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
            return PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.sharded_storage_db_list");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_list: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_list: pack overflow"); _ret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(context ? strlen(context) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_list: pack overflow"); _ret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, context, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(prefix ? strlen(prefix) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_list: pack overflow"); _ret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, prefix, _slen); _off += _slen; }
        }
        if (_off + sizeof(offset) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_list: pack overflow"); _ret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &offset, sizeof(offset)); _off += sizeof(offset);
        if (_off + sizeof(limit) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_list: pack overflow"); _ret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &limit, sizeof(limit)); _off += sizeof(limit);
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 65536);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_json, strdup(_msg))
                       : PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn < 5) { _ret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: truncated string response"); goto _rpc_done; }
        uint32_t _slen;
        memcpy(&_slen, _wbuf + 1, 4);
        if (_wn < (size_t)5 + _slen) { _ret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: truncated string payload"); goto _rpc_done; }
        char *_sv = malloc((size_t)_slen + 1);
        if (!_sv) { _ret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: out of memory"); goto _rpc_done; }
        if (_slen) memcpy(_sv, _wbuf + 5, _slen);
        _sv[_slen] = 0;
        _ret = PICOMESH_OK(picomesh_json, _sv); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_json, "sharded_storage_db_list: no impl on this class");
        return ((sharded_storage_db_list_fn)fn)(ctx, obj, hdrs, context, prefix, offset, limit);
    }
}

struct picomesh_json_result sharded_storage_db_list_all(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * prefix)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("sharded_storage", (method_id_t)sharded_storage_db_list_all);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_json, "sharded_storage_db_list_all: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_json, "sharded_storage_db_list_all: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_json_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(65539);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_json, "sharded_storage_db_list_all: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 2u);
            cmp_write_str(&_maw, context ? context : "", (uint32_t)(context ? strlen(context) : 0));
            cmp_write_str(&_maw, prefix ? prefix : "", (uint32_t)(prefix ? strlen(prefix) : 0));
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "sharded_storage.db.list_all", hdrs,
                                           _margs, _mab.offset, _mresp, 65539,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_json, strdup(_merr))
                            : PICOMESH_ERR(picomesh_json, "sharded_storage_db_list_all: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint32_t _mssz = 0;
            if (!cmp_read_str_size(&_mrr, &_mssz)) {
                _mret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list_all: bad msgpack string result");
            } else if (_mrb.offset + _mssz > _mrlen) {
                _mret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list_all: truncated msgpack string");
            } else {
                char *_msv = malloc((size_t)_mssz + 1);
                if (!_msv) _mret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list_all: out of memory");
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
            return PICOMESH_ERR(picomesh_json, "sharded_storage_db_list_all: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_json, "sharded_storage_db_list_all: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_json, "sharded_storage_db_list_all: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.sharded_storage_db_list_all");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_list_all: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list_all: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_list_all: pack overflow"); _ret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list_all: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(context ? strlen(context) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_list_all: pack overflow"); _ret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list_all: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, context, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(prefix ? strlen(prefix) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_list_all: pack overflow"); _ret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list_all: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, prefix, _slen); _off += _slen; }
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 65536);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list_all: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_json, strdup(_msg))
                       : PICOMESH_ERR(picomesh_json, "sharded_storage_db_list_all: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn < 5) { _ret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list_all: truncated string response"); goto _rpc_done; }
        uint32_t _slen;
        memcpy(&_slen, _wbuf + 1, 4);
        if (_wn < (size_t)5 + _slen) { _ret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list_all: truncated string payload"); goto _rpc_done; }
        char *_sv = malloc((size_t)_slen + 1);
        if (!_sv) { _ret = PICOMESH_ERR(picomesh_json, "sharded_storage_db_list_all: out of memory"); goto _rpc_done; }
        if (_slen) memcpy(_sv, _wbuf + 5, _slen);
        _sv[_slen] = 0;
        _ret = PICOMESH_OK(picomesh_json, _sv); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_json, "sharded_storage_db_list_all: no impl on this class");
        return ((sharded_storage_db_list_all_fn)fn)(ctx, obj, hdrs, context, prefix);
    }
}

struct picomesh_int64_result sharded_storage_db_incr(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key, int64_t delta)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("sharded_storage", (method_id_t)sharded_storage_db_incr);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_int64, "sharded_storage_db_incr: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_int64, "sharded_storage_db_incr: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_int64_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_int64, "sharded_storage_db_incr: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 3u);
            cmp_write_str(&_maw, context ? context : "", (uint32_t)(context ? strlen(context) : 0));
            cmp_write_str(&_maw, key ? key : "", (uint32_t)(key ? strlen(key) : 0));
            cmp_write_integer(&_maw, (int64_t)delta);
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "sharded_storage.db.incr", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_int64, strdup(_merr))
                            : PICOMESH_ERR(picomesh_int64, "sharded_storage_db_incr: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            int64_t _mv = 0;
            if (!cmp_read_integer(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_int64, "sharded_storage_db_incr: bad msgpack int result");
            else _mret = PICOMESH_OK(picomesh_int64, (int64_t)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_int64, "sharded_storage_db_incr: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_int64, "sharded_storage_db_incr: remote id unresolved");
        /* Wire scratch comes from THIS THREAD's pool, not the stack: the arg
         * buffer (_a, _acap bytes) and response buffer (_wbuf) are large
         * (~16 KiB + up to 64 KiB), and a nested chain of in-process hops would
         * overflow the fixed-size coroutine stack if these were locals. Every
         * exit below routes through _rpc_done, which returns both to the pool
         * exactly once (free(NULL) is a no-op). */
        struct picomesh_allocator *_pool = picomesh_allocator_thread();
        size_t _acap = 16384;
        struct picomesh_int64_result _ret;
        uint8_t *_a = (uint8_t *)picomesh_allocator_alloc(_pool, _acap);
        uint8_t *_wbuf = (uint8_t *)picomesh_allocator_alloc(_pool, 8197);
        if (!_a || !_wbuf) {
            picomesh_allocator_free(_pool, _a);
            picomesh_allocator_free(_pool, _wbuf);
            return PICOMESH_ERR(picomesh_int64, "sharded_storage_db_incr: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.sharded_storage_db_incr");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_incr: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_int64, "sharded_storage_db_incr: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_incr: pack overflow"); _ret = PICOMESH_ERR(picomesh_int64, "sharded_storage_db_incr: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(context ? strlen(context) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_incr: pack overflow"); _ret = PICOMESH_ERR(picomesh_int64, "sharded_storage_db_incr: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, context, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(key ? strlen(key) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_incr: pack overflow"); _ret = PICOMESH_ERR(picomesh_int64, "sharded_storage_db_incr: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, key, _slen); _off += _slen; }
        }
        if (_off + sizeof(delta) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_incr: pack overflow"); _ret = PICOMESH_ERR(picomesh_int64, "sharded_storage_db_incr: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &delta, sizeof(delta)); _off += sizeof(delta);
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_int64, "sharded_storage_db_incr: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_int64, strdup(_msg))
                       : PICOMESH_ERR(picomesh_int64, "sharded_storage_db_incr: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(int64_t)) { _ret = PICOMESH_ERR(picomesh_int64, "sharded_storage_db_incr: truncated RPC payload"); goto _rpc_done; }
        int64_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_int64, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_int64, "sharded_storage_db_incr: no impl on this class");
        return ((sharded_storage_db_incr_fn)fn)(ctx, obj, hdrs, context, key, delta);
    }
}

struct picomesh_int_result sharded_storage_db_put_if_absent(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key, const char * value)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("sharded_storage", (method_id_t)sharded_storage_db_put_if_absent);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_int, "sharded_storage_db_put_if_absent: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_int, "sharded_storage_db_put_if_absent: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_int_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_int, "sharded_storage_db_put_if_absent: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 3u);
            cmp_write_str(&_maw, context ? context : "", (uint32_t)(context ? strlen(context) : 0));
            cmp_write_str(&_maw, key ? key : "", (uint32_t)(key ? strlen(key) : 0));
            cmp_write_str(&_maw, value ? value : "", (uint32_t)(value ? strlen(value) : 0));
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "sharded_storage.db.put_if_absent", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_int, strdup(_merr))
                            : PICOMESH_ERR(picomesh_int, "sharded_storage_db_put_if_absent: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            int64_t _mv = 0;
            if (!cmp_read_integer(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_put_if_absent: bad msgpack int result");
            else _mret = PICOMESH_OK(picomesh_int, (int)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_int, "sharded_storage_db_put_if_absent: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_int, "sharded_storage_db_put_if_absent: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_int, "sharded_storage_db_put_if_absent: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.sharded_storage_db_put_if_absent");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_put_if_absent: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_put_if_absent: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_put_if_absent: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_put_if_absent: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(context ? strlen(context) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_put_if_absent: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_put_if_absent: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, context, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(key ? strlen(key) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_put_if_absent: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_put_if_absent: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, key, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(value ? strlen(value) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_put_if_absent: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_put_if_absent: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, value, _slen); _off += _slen; }
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_put_if_absent: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_int, strdup(_msg))
                       : PICOMESH_ERR(picomesh_int, "sharded_storage_db_put_if_absent: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(int)) { _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_put_if_absent: truncated RPC payload"); goto _rpc_done; }
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_int, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_int, "sharded_storage_db_put_if_absent: no impl on this class");
        return ((sharded_storage_db_put_if_absent_fn)fn)(ctx, obj, hdrs, context, key, value);
    }
}

struct picomesh_int_result sharded_storage_db_compare_and_set(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * context, const char * key, const char * expected, const char * replacement)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("sharded_storage", (method_id_t)sharded_storage_db_compare_and_set);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_int, "sharded_storage_db_compare_and_set: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_int, "sharded_storage_db_compare_and_set: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_int_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_int, "sharded_storage_db_compare_and_set: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 4u);
            cmp_write_str(&_maw, context ? context : "", (uint32_t)(context ? strlen(context) : 0));
            cmp_write_str(&_maw, key ? key : "", (uint32_t)(key ? strlen(key) : 0));
            cmp_write_str(&_maw, expected ? expected : "", (uint32_t)(expected ? strlen(expected) : 0));
            cmp_write_str(&_maw, replacement ? replacement : "", (uint32_t)(replacement ? strlen(replacement) : 0));
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "sharded_storage.db.compare_and_set", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_int, strdup(_merr))
                            : PICOMESH_ERR(picomesh_int, "sharded_storage_db_compare_and_set: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            int64_t _mv = 0;
            if (!cmp_read_integer(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_compare_and_set: bad msgpack int result");
            else _mret = PICOMESH_OK(picomesh_int, (int)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_int, "sharded_storage_db_compare_and_set: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_int, "sharded_storage_db_compare_and_set: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_int, "sharded_storage_db_compare_and_set: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.sharded_storage_db_compare_and_set");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_compare_and_set: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_compare_and_set: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_compare_and_set: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_compare_and_set: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(context ? strlen(context) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_compare_and_set: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_compare_and_set: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, context, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(key ? strlen(key) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_compare_and_set: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_compare_and_set: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, key, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(expected ? strlen(expected) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_compare_and_set: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_compare_and_set: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, expected, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(replacement ? strlen(replacement) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "sharded_storage_db_compare_and_set: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_compare_and_set: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, replacement, _slen); _off += _slen; }
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_compare_and_set: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_int, strdup(_msg))
                       : PICOMESH_ERR(picomesh_int, "sharded_storage_db_compare_and_set: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(int)) { _ret = PICOMESH_ERR(picomesh_int, "sharded_storage_db_compare_and_set: truncated RPC payload"); goto _rpc_done; }
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_int, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_int, "sharded_storage_db_compare_and_set: no impl on this class");
        return ((sharded_storage_db_compare_and_set_fn)fn)(ctx, obj, hdrs, context, key, expected, replacement);
    }
}

