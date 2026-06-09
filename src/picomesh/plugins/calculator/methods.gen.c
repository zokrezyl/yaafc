/* GENERATED — do not edit. */
#include "calculator.internal.h"
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

struct picomesh_int64_result calculator_calc_add(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, int64_t x, int64_t y)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("calculator", (method_id_t)calculator_calc_add);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_int64, "calculator_calc_add: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_int64, "calculator_calc_add: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_int64_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_int64, "calculator_calc_add: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 2u);
            cmp_write_integer(&_maw, (int64_t)x);
            cmp_write_integer(&_maw, (int64_t)y);
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "calculator.calc.add", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_int64, strdup(_merr))
                            : PICOMESH_ERR(picomesh_int64, "calculator_calc_add: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            int64_t _mv = 0;
            if (!cmp_read_integer(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_int64, "calculator_calc_add: bad msgpack int result");
            else _mret = PICOMESH_OK(picomesh_int64, (int64_t)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_int64, "calculator_calc_add: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_int64, "calculator_calc_add: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_int64, "calculator_calc_add: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.calculator_calc_add");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "calculator_calc_add: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_add: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "calculator_calc_add: pack overflow"); _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_add: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(x) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "calculator_calc_add: pack overflow"); _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_add: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &x, sizeof(x)); _off += sizeof(x);
        if (_off + sizeof(y) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "calculator_calc_add: pack overflow"); _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_add: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &y, sizeof(y)); _off += sizeof(y);
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_add: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_int64, strdup(_msg))
                       : PICOMESH_ERR(picomesh_int64, "calculator_calc_add: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(int64_t)) { _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_add: truncated RPC payload"); goto _rpc_done; }
        int64_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_int64, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_int64, "calculator_calc_add: no impl on this class");
        return ((calculator_calc_add_fn)fn)(ctx, obj, hdrs, x, y);
    }
}

struct picomesh_int64_result calculator_calc_sub(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, int64_t x, int64_t y)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("calculator", (method_id_t)calculator_calc_sub);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_int64, "calculator_calc_sub: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_int64, "calculator_calc_sub: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_int64_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_int64, "calculator_calc_sub: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 2u);
            cmp_write_integer(&_maw, (int64_t)x);
            cmp_write_integer(&_maw, (int64_t)y);
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "calculator.calc.sub", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_int64, strdup(_merr))
                            : PICOMESH_ERR(picomesh_int64, "calculator_calc_sub: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            int64_t _mv = 0;
            if (!cmp_read_integer(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_int64, "calculator_calc_sub: bad msgpack int result");
            else _mret = PICOMESH_OK(picomesh_int64, (int64_t)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_int64, "calculator_calc_sub: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_int64, "calculator_calc_sub: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_int64, "calculator_calc_sub: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.calculator_calc_sub");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "calculator_calc_sub: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_sub: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "calculator_calc_sub: pack overflow"); _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_sub: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(x) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "calculator_calc_sub: pack overflow"); _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_sub: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &x, sizeof(x)); _off += sizeof(x);
        if (_off + sizeof(y) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "calculator_calc_sub: pack overflow"); _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_sub: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &y, sizeof(y)); _off += sizeof(y);
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_sub: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_int64, strdup(_msg))
                       : PICOMESH_ERR(picomesh_int64, "calculator_calc_sub: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(int64_t)) { _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_sub: truncated RPC payload"); goto _rpc_done; }
        int64_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_int64, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_int64, "calculator_calc_sub: no impl on this class");
        return ((calculator_calc_sub_fn)fn)(ctx, obj, hdrs, x, y);
    }
}

struct picomesh_int64_result calculator_calc_mul(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, int64_t x, int64_t y)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("calculator", (method_id_t)calculator_calc_mul);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_int64, "calculator_calc_mul: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_int64, "calculator_calc_mul: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_int64_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_int64, "calculator_calc_mul: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 2u);
            cmp_write_integer(&_maw, (int64_t)x);
            cmp_write_integer(&_maw, (int64_t)y);
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "calculator.calc.mul", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_int64, strdup(_merr))
                            : PICOMESH_ERR(picomesh_int64, "calculator_calc_mul: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            int64_t _mv = 0;
            if (!cmp_read_integer(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_int64, "calculator_calc_mul: bad msgpack int result");
            else _mret = PICOMESH_OK(picomesh_int64, (int64_t)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_int64, "calculator_calc_mul: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_int64, "calculator_calc_mul: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_int64, "calculator_calc_mul: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.calculator_calc_mul");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "calculator_calc_mul: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_mul: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "calculator_calc_mul: pack overflow"); _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_mul: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(x) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "calculator_calc_mul: pack overflow"); _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_mul: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &x, sizeof(x)); _off += sizeof(x);
        if (_off + sizeof(y) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "calculator_calc_mul: pack overflow"); _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_mul: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &y, sizeof(y)); _off += sizeof(y);
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_mul: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_int64, strdup(_msg))
                       : PICOMESH_ERR(picomesh_int64, "calculator_calc_mul: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(int64_t)) { _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_mul: truncated RPC payload"); goto _rpc_done; }
        int64_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_int64, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_int64, "calculator_calc_mul: no impl on this class");
        return ((calculator_calc_mul_fn)fn)(ctx, obj, hdrs, x, y);
    }
}

struct picomesh_int64_result calculator_calc_div(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, int64_t x, int64_t y)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("calculator", (method_id_t)calculator_calc_div);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_int64, "calculator_calc_div: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_int64, "calculator_calc_div: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_int64_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_int64, "calculator_calc_div: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 2u);
            cmp_write_integer(&_maw, (int64_t)x);
            cmp_write_integer(&_maw, (int64_t)y);
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "calculator.calc.div", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_int64, strdup(_merr))
                            : PICOMESH_ERR(picomesh_int64, "calculator_calc_div: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            int64_t _mv = 0;
            if (!cmp_read_integer(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_int64, "calculator_calc_div: bad msgpack int result");
            else _mret = PICOMESH_OK(picomesh_int64, (int64_t)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_int64, "calculator_calc_div: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_int64, "calculator_calc_div: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_int64, "calculator_calc_div: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.calculator_calc_div");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "calculator_calc_div: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_div: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "calculator_calc_div: pack overflow"); _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_div: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(x) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "calculator_calc_div: pack overflow"); _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_div: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &x, sizeof(x)); _off += sizeof(x);
        if (_off + sizeof(y) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "calculator_calc_div: pack overflow"); _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_div: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &y, sizeof(y)); _off += sizeof(y);
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_div: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_int64, strdup(_msg))
                       : PICOMESH_ERR(picomesh_int64, "calculator_calc_div: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(int64_t)) { _ret = PICOMESH_ERR(picomesh_int64, "calculator_calc_div: truncated RPC payload"); goto _rpc_done; }
        int64_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_int64, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_int64, "calculator_calc_div: no impl on this class");
        return ((calculator_calc_div_fn)fn)(ctx, obj, hdrs, x, y);
    }
}

