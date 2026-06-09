/* GENERATED — do not edit. */
#include "git_repo.internal.h"
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

struct picomesh_uint32_result git_repo_git_repo_make(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t owner_id, const char * owner_name, const char * repo_name)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("git_repo", (method_id_t)git_repo_git_repo_make);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_make: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_make: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_uint32_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_make: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 3u);
            cmp_write_uinteger(&_maw, (uint64_t)owner_id);
            cmp_write_str(&_maw, owner_name ? owner_name : "", (uint32_t)(owner_name ? strlen(owner_name) : 0));
            cmp_write_str(&_maw, repo_name ? repo_name : "", (uint32_t)(repo_name ? strlen(repo_name) : 0));
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "git_repo.git_repo.make", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_uint32, strdup(_merr))
                            : PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_make: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint64_t _mv = 0;
            if (!cmp_read_uinteger(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_make: bad msgpack uint result");
            else _mret = PICOMESH_OK(picomesh_uint32, (uint32_t)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_make: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_make: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_make: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.git_repo_git_repo_make");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_make: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_make: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_make: pack overflow"); _ret = PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_make: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(owner_id) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_make: pack overflow"); _ret = PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_make: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &owner_id, sizeof(owner_id)); _off += sizeof(owner_id);
        {
            uint32_t _slen = (uint32_t)(owner_name ? strlen(owner_name) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_make: pack overflow"); _ret = PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_make: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, owner_name, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(repo_name ? strlen(repo_name) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_make: pack overflow"); _ret = PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_make: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, repo_name, _slen); _off += _slen; }
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_make: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_uint32, strdup(_msg))
                       : PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_make: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(uint32_t)) { _ret = PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_make: truncated RPC payload"); goto _rpc_done; }
        uint32_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_uint32, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_make: no impl on this class");
        return ((git_repo_git_repo_make_fn)fn)(ctx, obj, hdrs, owner_id, owner_name, repo_name);
    }
}

struct picomesh_int_result git_repo_git_repo_delete(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t repo_id)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("git_repo", (method_id_t)git_repo_git_repo_delete);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_int, "git_repo_git_repo_delete: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_int, "git_repo_git_repo_delete: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_int_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_int, "git_repo_git_repo_delete: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 1u);
            cmp_write_uinteger(&_maw, (uint64_t)repo_id);
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "git_repo.git_repo.delete", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_int, strdup(_merr))
                            : PICOMESH_ERR(picomesh_int, "git_repo_git_repo_delete: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            int64_t _mv = 0;
            if (!cmp_read_integer(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_int, "git_repo_git_repo_delete: bad msgpack int result");
            else _mret = PICOMESH_OK(picomesh_int, (int)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_int, "git_repo_git_repo_delete: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_int, "git_repo_git_repo_delete: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_int, "git_repo_git_repo_delete: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.git_repo_git_repo_delete");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_delete: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_int, "git_repo_git_repo_delete: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_delete: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "git_repo_git_repo_delete: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(repo_id) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_delete: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "git_repo_git_repo_delete: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &repo_id, sizeof(repo_id)); _off += sizeof(repo_id);
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_int, "git_repo_git_repo_delete: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_int, strdup(_msg))
                       : PICOMESH_ERR(picomesh_int, "git_repo_git_repo_delete: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(int)) { _ret = PICOMESH_ERR(picomesh_int, "git_repo_git_repo_delete: truncated RPC payload"); goto _rpc_done; }
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_int, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_int, "git_repo_git_repo_delete: no impl on this class");
        return ((git_repo_git_repo_delete_fn)fn)(ctx, obj, hdrs, repo_id);
    }
}

struct picomesh_uint32_result git_repo_git_repo_owner_of(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t repo_id)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("git_repo", (method_id_t)git_repo_git_repo_owner_of);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_owner_of: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_owner_of: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_uint32_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_owner_of: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 1u);
            cmp_write_uinteger(&_maw, (uint64_t)repo_id);
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "git_repo.git_repo.owner_of", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_uint32, strdup(_merr))
                            : PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_owner_of: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint64_t _mv = 0;
            if (!cmp_read_uinteger(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_owner_of: bad msgpack uint result");
            else _mret = PICOMESH_OK(picomesh_uint32, (uint32_t)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_owner_of: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_owner_of: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_owner_of: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.git_repo_git_repo_owner_of");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_owner_of: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_owner_of: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_owner_of: pack overflow"); _ret = PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_owner_of: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(repo_id) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_owner_of: pack overflow"); _ret = PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_owner_of: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &repo_id, sizeof(repo_id)); _off += sizeof(repo_id);
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_owner_of: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_uint32, strdup(_msg))
                       : PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_owner_of: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(uint32_t)) { _ret = PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_owner_of: truncated RPC payload"); goto _rpc_done; }
        uint32_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_uint32, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_uint32, "git_repo_git_repo_owner_of: no impl on this class");
        return ((git_repo_git_repo_owner_of_fn)fn)(ctx, obj, hdrs, repo_id);
    }
}

struct picomesh_string_result git_repo_git_repo_namespace_of(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t repo_id)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("git_repo", (method_id_t)git_repo_git_repo_namespace_of);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_namespace_of: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_namespace_of: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_string_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(65539);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_namespace_of: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 1u);
            cmp_write_uinteger(&_maw, (uint64_t)repo_id);
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "git_repo.git_repo.namespace_of", hdrs,
                                           _margs, _mab.offset, _mresp, 65539,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_string, strdup(_merr))
                            : PICOMESH_ERR(picomesh_string, "git_repo_git_repo_namespace_of: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint32_t _mssz = 0;
            if (!cmp_read_str_size(&_mrr, &_mssz)) {
                _mret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_namespace_of: bad msgpack string result");
            } else if (_mrb.offset + _mssz > _mrlen) {
                _mret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_namespace_of: truncated msgpack string");
            } else {
                char *_msv = malloc((size_t)_mssz + 1);
                if (!_msv) _mret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_namespace_of: out of memory");
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
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_namespace_of: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_namespace_of: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_namespace_of: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.git_repo_git_repo_namespace_of");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_namespace_of: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_namespace_of: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_namespace_of: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_namespace_of: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(repo_id) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_namespace_of: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_namespace_of: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &repo_id, sizeof(repo_id)); _off += sizeof(repo_id);
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 65536);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_namespace_of: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_string, strdup(_msg))
                       : PICOMESH_ERR(picomesh_string, "git_repo_git_repo_namespace_of: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn < 5) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_namespace_of: truncated string response"); goto _rpc_done; }
        uint32_t _slen;
        memcpy(&_slen, _wbuf + 1, 4);
        if (_wn < (size_t)5 + _slen) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_namespace_of: truncated string payload"); goto _rpc_done; }
        char *_sv = malloc((size_t)_slen + 1);
        if (!_sv) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_namespace_of: out of memory"); goto _rpc_done; }
        if (_slen) memcpy(_sv, _wbuf + 5, _slen);
        _sv[_slen] = 0;
        _ret = PICOMESH_OK(picomesh_string, _sv); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_namespace_of: no impl on this class");
        return ((git_repo_git_repo_namespace_of_fn)fn)(ctx, obj, hdrs, repo_id);
    }
}

struct picomesh_size_result git_repo_git_repo_count_for_owner(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t owner_id)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("git_repo", (method_id_t)git_repo_git_repo_count_for_owner);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_owner: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_owner: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_size_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_owner: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 1u);
            cmp_write_uinteger(&_maw, (uint64_t)owner_id);
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "git_repo.git_repo.count_for_owner", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_size, strdup(_merr))
                            : PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_owner: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint64_t _mv = 0;
            if (!cmp_read_uinteger(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_owner: bad msgpack uint result");
            else _mret = PICOMESH_OK(picomesh_size, (size_t)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_owner: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_owner: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_owner: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.git_repo_git_repo_count_for_owner");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_count_for_owner: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_owner: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_count_for_owner: pack overflow"); _ret = PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_owner: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(owner_id) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_count_for_owner: pack overflow"); _ret = PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_owner: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &owner_id, sizeof(owner_id)); _off += sizeof(owner_id);
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_owner: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_size, strdup(_msg))
                       : PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_owner: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(size_t)) { _ret = PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_owner: truncated RPC payload"); goto _rpc_done; }
        size_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_size, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_owner: no impl on this class");
        return ((git_repo_git_repo_count_for_owner_fn)fn)(ctx, obj, hdrs, owner_id);
    }
}

struct picomesh_size_result git_repo_git_repo_count_total(struct ctx * ctx, struct object * obj, struct yheaders * hdrs)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("git_repo", (method_id_t)git_repo_git_repo_count_total);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_total: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_total: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_size_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_total: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 0u);
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "git_repo.git_repo.count_total", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_size, strdup(_merr))
                            : PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_total: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint64_t _mv = 0;
            if (!cmp_read_uinteger(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_total: bad msgpack uint result");
            else _mret = PICOMESH_OK(picomesh_size, (size_t)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_total: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_total: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_total: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.git_repo_git_repo_count_total");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_count_total: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_total: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_count_total: pack overflow"); _ret = PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_total: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_total: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_size, strdup(_msg))
                       : PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_total: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(size_t)) { _ret = PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_total: truncated RPC payload"); goto _rpc_done; }
        size_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_size, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_total: no impl on this class");
        return ((git_repo_git_repo_count_total_fn)fn)(ctx, obj, hdrs);
    }
}

struct picomesh_string_result git_repo_git_repo_list_for_owner(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t owner_id)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("git_repo", (method_id_t)git_repo_git_repo_list_for_owner);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_owner: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_owner: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_string_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(65539);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_owner: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 1u);
            cmp_write_uinteger(&_maw, (uint64_t)owner_id);
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "git_repo.git_repo.list_for_owner", hdrs,
                                           _margs, _mab.offset, _mresp, 65539,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_string, strdup(_merr))
                            : PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_owner: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint32_t _mssz = 0;
            if (!cmp_read_str_size(&_mrr, &_mssz)) {
                _mret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_owner: bad msgpack string result");
            } else if (_mrb.offset + _mssz > _mrlen) {
                _mret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_owner: truncated msgpack string");
            } else {
                char *_msv = malloc((size_t)_mssz + 1);
                if (!_msv) _mret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_owner: out of memory");
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
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_owner: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_owner: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_owner: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.git_repo_git_repo_list_for_owner");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_list_for_owner: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_owner: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_list_for_owner: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_owner: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(owner_id) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_list_for_owner: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_owner: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &owner_id, sizeof(owner_id)); _off += sizeof(owner_id);
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 65536);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_owner: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_string, strdup(_msg))
                       : PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_owner: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn < 5) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_owner: truncated string response"); goto _rpc_done; }
        uint32_t _slen;
        memcpy(&_slen, _wbuf + 1, 4);
        if (_wn < (size_t)5 + _slen) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_owner: truncated string payload"); goto _rpc_done; }
        char *_sv = malloc((size_t)_slen + 1);
        if (!_sv) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_owner: out of memory"); goto _rpc_done; }
        if (_slen) memcpy(_sv, _wbuf + 5, _slen);
        _sv[_slen] = 0;
        _ret = PICOMESH_OK(picomesh_string, _sv); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_owner: no impl on this class");
        return ((git_repo_git_repo_list_for_owner_fn)fn)(ctx, obj, hdrs, owner_id);
    }
}

struct picomesh_string_result git_repo_git_repo_list_for_namespace(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * path)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("git_repo", (method_id_t)git_repo_git_repo_list_for_namespace);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_namespace: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_namespace: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_string_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(65539);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_namespace: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 1u);
            cmp_write_str(&_maw, path ? path : "", (uint32_t)(path ? strlen(path) : 0));
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "git_repo.git_repo.list_for_namespace", hdrs,
                                           _margs, _mab.offset, _mresp, 65539,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_string, strdup(_merr))
                            : PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_namespace: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint32_t _mssz = 0;
            if (!cmp_read_str_size(&_mrr, &_mssz)) {
                _mret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_namespace: bad msgpack string result");
            } else if (_mrb.offset + _mssz > _mrlen) {
                _mret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_namespace: truncated msgpack string");
            } else {
                char *_msv = malloc((size_t)_mssz + 1);
                if (!_msv) _mret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_namespace: out of memory");
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
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_namespace: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_namespace: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_namespace: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.git_repo_git_repo_list_for_namespace");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_list_for_namespace: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_namespace: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_list_for_namespace: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_namespace: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(path ? strlen(path) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_list_for_namespace: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_namespace: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, path, _slen); _off += _slen; }
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 65536);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_namespace: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_string, strdup(_msg))
                       : PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_namespace: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn < 5) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_namespace: truncated string response"); goto _rpc_done; }
        uint32_t _slen;
        memcpy(&_slen, _wbuf + 1, 4);
        if (_wn < (size_t)5 + _slen) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_namespace: truncated string payload"); goto _rpc_done; }
        char *_sv = malloc((size_t)_slen + 1);
        if (!_sv) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_namespace: out of memory"); goto _rpc_done; }
        if (_slen) memcpy(_sv, _wbuf + 5, _slen);
        _sv[_slen] = 0;
        _ret = PICOMESH_OK(picomesh_string, _sv); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_list_for_namespace: no impl on this class");
        return ((git_repo_git_repo_list_for_namespace_fn)fn)(ctx, obj, hdrs, path);
    }
}

struct picomesh_size_result git_repo_git_repo_count_for_namespace(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, const char * path)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("git_repo", (method_id_t)git_repo_git_repo_count_for_namespace);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_namespace: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_namespace: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_size_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_namespace: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 1u);
            cmp_write_str(&_maw, path ? path : "", (uint32_t)(path ? strlen(path) : 0));
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "git_repo.git_repo.count_for_namespace", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_size, strdup(_merr))
                            : PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_namespace: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint64_t _mv = 0;
            if (!cmp_read_uinteger(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_namespace: bad msgpack uint result");
            else _mret = PICOMESH_OK(picomesh_size, (size_t)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_namespace: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_namespace: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_namespace: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.git_repo_git_repo_count_for_namespace");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_count_for_namespace: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_namespace: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_count_for_namespace: pack overflow"); _ret = PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_namespace: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        {
            uint32_t _slen = (uint32_t)(path ? strlen(path) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_count_for_namespace: pack overflow"); _ret = PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_namespace: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, path, _slen); _off += _slen; }
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_namespace: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_size, strdup(_msg))
                       : PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_namespace: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(size_t)) { _ret = PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_namespace: truncated RPC payload"); goto _rpc_done; }
        size_t _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_size, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_namespace: no impl on this class");
        return ((git_repo_git_repo_count_for_namespace_fn)fn)(ctx, obj, hdrs, path);
    }
}

struct picomesh_string_result git_repo_git_repo_read_tree(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t repo_id, const char * ref, const char * path)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("git_repo", (method_id_t)git_repo_git_repo_read_tree);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_tree: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_tree: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_string_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(65539);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_tree: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 3u);
            cmp_write_uinteger(&_maw, (uint64_t)repo_id);
            cmp_write_str(&_maw, ref ? ref : "", (uint32_t)(ref ? strlen(ref) : 0));
            cmp_write_str(&_maw, path ? path : "", (uint32_t)(path ? strlen(path) : 0));
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "git_repo.git_repo.read_tree", hdrs,
                                           _margs, _mab.offset, _mresp, 65539,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_string, strdup(_merr))
                            : PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_tree: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint32_t _mssz = 0;
            if (!cmp_read_str_size(&_mrr, &_mssz)) {
                _mret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_tree: bad msgpack string result");
            } else if (_mrb.offset + _mssz > _mrlen) {
                _mret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_tree: truncated msgpack string");
            } else {
                char *_msv = malloc((size_t)_mssz + 1);
                if (!_msv) _mret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_tree: out of memory");
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
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_tree: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_tree: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_tree: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.git_repo_git_repo_read_tree");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_read_tree: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_tree: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_read_tree: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_tree: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(repo_id) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_read_tree: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_tree: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &repo_id, sizeof(repo_id)); _off += sizeof(repo_id);
        {
            uint32_t _slen = (uint32_t)(ref ? strlen(ref) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_read_tree: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_tree: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, ref, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(path ? strlen(path) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_read_tree: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_tree: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, path, _slen); _off += _slen; }
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 65536);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_tree: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_string, strdup(_msg))
                       : PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_tree: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn < 5) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_tree: truncated string response"); goto _rpc_done; }
        uint32_t _slen;
        memcpy(&_slen, _wbuf + 1, 4);
        if (_wn < (size_t)5 + _slen) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_tree: truncated string payload"); goto _rpc_done; }
        char *_sv = malloc((size_t)_slen + 1);
        if (!_sv) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_tree: out of memory"); goto _rpc_done; }
        if (_slen) memcpy(_sv, _wbuf + 5, _slen);
        _sv[_slen] = 0;
        _ret = PICOMESH_OK(picomesh_string, _sv); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_tree: no impl on this class");
        return ((git_repo_git_repo_read_tree_fn)fn)(ctx, obj, hdrs, repo_id, ref, path);
    }
}

struct picomesh_string_result git_repo_git_repo_read_file(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t repo_id, const char * ref, const char * path)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("git_repo", (method_id_t)git_repo_git_repo_read_file);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_file: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_file: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_string_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(65539);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_file: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 3u);
            cmp_write_uinteger(&_maw, (uint64_t)repo_id);
            cmp_write_str(&_maw, ref ? ref : "", (uint32_t)(ref ? strlen(ref) : 0));
            cmp_write_str(&_maw, path ? path : "", (uint32_t)(path ? strlen(path) : 0));
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "git_repo.git_repo.read_file", hdrs,
                                           _margs, _mab.offset, _mresp, 65539,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_string, strdup(_merr))
                            : PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_file: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint32_t _mssz = 0;
            if (!cmp_read_str_size(&_mrr, &_mssz)) {
                _mret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_file: bad msgpack string result");
            } else if (_mrb.offset + _mssz > _mrlen) {
                _mret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_file: truncated msgpack string");
            } else {
                char *_msv = malloc((size_t)_mssz + 1);
                if (!_msv) _mret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_file: out of memory");
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
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_file: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_file: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_file: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.git_repo_git_repo_read_file");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_read_file: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_file: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_read_file: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_file: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(repo_id) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_read_file: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_file: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &repo_id, sizeof(repo_id)); _off += sizeof(repo_id);
        {
            uint32_t _slen = (uint32_t)(ref ? strlen(ref) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_read_file: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_file: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, ref, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(path ? strlen(path) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_read_file: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_file: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, path, _slen); _off += _slen; }
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 65536);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_file: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_string, strdup(_msg))
                       : PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_file: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn < 5) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_file: truncated string response"); goto _rpc_done; }
        uint32_t _slen;
        memcpy(&_slen, _wbuf + 1, 4);
        if (_wn < (size_t)5 + _slen) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_file: truncated string payload"); goto _rpc_done; }
        char *_sv = malloc((size_t)_slen + 1);
        if (!_sv) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_file: out of memory"); goto _rpc_done; }
        if (_slen) memcpy(_sv, _wbuf + 5, _slen);
        _sv[_slen] = 0;
        _ret = PICOMESH_OK(picomesh_string, _sv); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_read_file: no impl on this class");
        return ((git_repo_git_repo_read_file_fn)fn)(ctx, obj, hdrs, repo_id, ref, path);
    }
}

struct picomesh_string_result git_repo_git_repo_put_file(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t repo_id, const char * path, const char * content, const char * message, const char * author_name, const char * author_email)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("git_repo", (method_id_t)git_repo_git_repo_put_file);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_string_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(65539);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 6u);
            cmp_write_uinteger(&_maw, (uint64_t)repo_id);
            cmp_write_str(&_maw, path ? path : "", (uint32_t)(path ? strlen(path) : 0));
            cmp_write_str(&_maw, content ? content : "", (uint32_t)(content ? strlen(content) : 0));
            cmp_write_str(&_maw, message ? message : "", (uint32_t)(message ? strlen(message) : 0));
            cmp_write_str(&_maw, author_name ? author_name : "", (uint32_t)(author_name ? strlen(author_name) : 0));
            cmp_write_str(&_maw, author_email ? author_email : "", (uint32_t)(author_email ? strlen(author_email) : 0));
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "git_repo.git_repo.put_file", hdrs,
                                           _margs, _mab.offset, _mresp, 65539,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_string, strdup(_merr))
                            : PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint32_t _mssz = 0;
            if (!cmp_read_str_size(&_mrr, &_mssz)) {
                _mret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: bad msgpack string result");
            } else if (_mrb.offset + _mssz > _mrlen) {
                _mret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: truncated msgpack string");
            } else {
                char *_msv = malloc((size_t)_mssz + 1);
                if (!_msv) _mret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: out of memory");
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
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.git_repo_git_repo_put_file");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_put_file: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_put_file: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(repo_id) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_put_file: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &repo_id, sizeof(repo_id)); _off += sizeof(repo_id);
        {
            uint32_t _slen = (uint32_t)(path ? strlen(path) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_put_file: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, path, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(content ? strlen(content) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_put_file: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, content, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(message ? strlen(message) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_put_file: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, message, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(author_name ? strlen(author_name) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_put_file: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, author_name, _slen); _off += _slen; }
        }
        {
            uint32_t _slen = (uint32_t)(author_email ? strlen(author_email) : 0);
            if (_off + 4 + _slen > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_put_file: pack overflow"); _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_slen, 4); _off += 4;
            if (_slen) { memcpy(_a + _off, author_email, _slen); _off += _slen; }
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 65536);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_string, strdup(_msg))
                       : PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn < 5) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: truncated string response"); goto _rpc_done; }
        uint32_t _slen;
        memcpy(&_slen, _wbuf + 1, 4);
        if (_wn < (size_t)5 + _slen) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: truncated string payload"); goto _rpc_done; }
        char *_sv = malloc((size_t)_slen + 1);
        if (!_sv) { _ret = PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: out of memory"); goto _rpc_done; }
        if (_slen) memcpy(_sv, _wbuf + 5, _slen);
        _sv[_slen] = 0;
        _ret = PICOMESH_OK(picomesh_string, _sv); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_string, "git_repo_git_repo_put_file: no impl on this class");
        return ((git_repo_git_repo_put_file_fn)fn)(ctx, obj, hdrs, repo_id, path, content, message, author_name, author_email);
    }
}

struct picomesh_int_result git_repo_git_repo_is_public(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t repo_id)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("git_repo", (method_id_t)git_repo_git_repo_is_public);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_int, "git_repo_git_repo_is_public: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_int, "git_repo_git_repo_is_public: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_int_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_int, "git_repo_git_repo_is_public: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 1u);
            cmp_write_uinteger(&_maw, (uint64_t)repo_id);
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "git_repo.git_repo.is_public", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_int, strdup(_merr))
                            : PICOMESH_ERR(picomesh_int, "git_repo_git_repo_is_public: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            int64_t _mv = 0;
            if (!cmp_read_integer(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_int, "git_repo_git_repo_is_public: bad msgpack int result");
            else _mret = PICOMESH_OK(picomesh_int, (int)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_int, "git_repo_git_repo_is_public: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_int, "git_repo_git_repo_is_public: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_int, "git_repo_git_repo_is_public: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.git_repo_git_repo_is_public");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_is_public: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_int, "git_repo_git_repo_is_public: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_is_public: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "git_repo_git_repo_is_public: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(repo_id) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_is_public: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "git_repo_git_repo_is_public: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &repo_id, sizeof(repo_id)); _off += sizeof(repo_id);
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_int, "git_repo_git_repo_is_public: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_int, strdup(_msg))
                       : PICOMESH_ERR(picomesh_int, "git_repo_git_repo_is_public: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(int)) { _ret = PICOMESH_ERR(picomesh_int, "git_repo_git_repo_is_public: truncated RPC payload"); goto _rpc_done; }
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_int, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_int, "git_repo_git_repo_is_public: no impl on this class");
        return ((git_repo_git_repo_is_public_fn)fn)(ctx, obj, hdrs, repo_id);
    }
}

struct picomesh_int_result git_repo_git_repo_set_public(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, uint32_t repo_id, int is_public)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("git_repo", (method_id_t)git_repo_git_repo_set_public);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_int, "git_repo_git_repo_set_public: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_int, "git_repo_git_repo_set_public: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_int_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(256);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_int, "git_repo_git_repo_set_public: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 2u);
            cmp_write_uinteger(&_maw, (uint64_t)repo_id);
            cmp_write_integer(&_maw, (int64_t)is_public);
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "git_repo.git_repo.set_public", hdrs,
                                           _margs, _mab.offset, _mresp, 256,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_int, strdup(_merr))
                            : PICOMESH_ERR(picomesh_int, "git_repo_git_repo_set_public: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            int64_t _mv = 0;
            if (!cmp_read_integer(&_mrr, &_mv))
                _mret = PICOMESH_ERR(picomesh_int, "git_repo_git_repo_set_public: bad msgpack int result");
            else _mret = PICOMESH_OK(picomesh_int, (int)_mv);
            }
            free(_margs); free(_mresp);
            return _mret;
        }
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR(picomesh_int, "git_repo_git_repo_set_public: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_int, "git_repo_git_repo_set_public: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_int, "git_repo_git_repo_set_public: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.git_repo_git_repo_set_public");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_set_public: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_int, "git_repo_git_repo_set_public: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_set_public: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "git_repo_git_repo_set_public: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(repo_id) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_set_public: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "git_repo_git_repo_set_public: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &repo_id, sizeof(repo_id)); _off += sizeof(repo_id);
        if (_off + sizeof(is_public) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_set_public: pack overflow"); _ret = PICOMESH_ERR(picomesh_int, "git_repo_git_repo_set_public: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &is_public, sizeof(is_public)); _off += sizeof(is_public);
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 8197);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_int, "git_repo_git_repo_set_public: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_int, strdup(_msg))
                       : PICOMESH_ERR(picomesh_int, "git_repo_git_repo_set_public: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn != 1 + sizeof(int)) { _ret = PICOMESH_ERR(picomesh_int, "git_repo_git_repo_set_public: truncated RPC payload"); goto _rpc_done; }
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK(picomesh_int, _v); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_int, "git_repo_git_repo_set_public: no impl on this class");
        return ((git_repo_git_repo_set_public_fn)fn)(ctx, obj, hdrs, repo_id, is_public);
    }
}

struct picomesh_json_result git_repo_git_repo_list(struct ctx * ctx, struct object * obj, struct yheaders * hdrs, int64_t offset, int64_t limit)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("git_repo", (method_id_t)git_repo_git_repo_list);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_json_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(65539);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 2u);
            cmp_write_integer(&_maw, (int64_t)offset);
            cmp_write_integer(&_maw, (int64_t)limit);
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "git_repo.git_repo.list", hdrs,
                                           _margs, _mab.offset, _mresp, 65539,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_json, strdup(_merr))
                            : PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint32_t _mssz = 0;
            if (!cmp_read_str_size(&_mrr, &_mssz)) {
                _mret = PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list: bad msgpack string result");
            } else if (_mrb.offset + _mssz > _mrlen) {
                _mret = PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list: truncated msgpack string");
            } else {
                char *_msv = malloc((size_t)_mssz + 1);
                if (!_msv) _mret = PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list: out of memory");
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
            return PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.git_repo_git_repo_list");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_list: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_list: pack overflow"); _ret = PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        if (_off + sizeof(offset) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_list: pack overflow"); _ret = PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &offset, sizeof(offset)); _off += sizeof(offset);
        if (_off + sizeof(limit) > _acap)
            { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_list: pack overflow"); _ret = PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list: pack overflow"); goto _rpc_done; }
        memcpy(_a + _off, &limit, sizeof(limit)); _off += sizeof(limit);
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 65536);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_json, strdup(_msg))
                       : PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn < 5) { _ret = PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list: truncated string response"); goto _rpc_done; }
        uint32_t _slen;
        memcpy(&_slen, _wbuf + 1, 4);
        if (_wn < (size_t)5 + _slen) { _ret = PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list: truncated string payload"); goto _rpc_done; }
        char *_sv = malloc((size_t)_slen + 1);
        if (!_sv) { _ret = PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list: out of memory"); goto _rpc_done; }
        if (_slen) memcpy(_sv, _wbuf + 5, _slen);
        _sv[_slen] = 0;
        _ret = PICOMESH_OK(picomesh_json, _sv); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list: no impl on this class");
        return ((git_repo_git_repo_list_fn)fn)(ctx, obj, hdrs, offset, limit);
    }
}

struct picomesh_json_result git_repo_git_repo_list_all(struct ctx * ctx, struct object * obj, struct yheaders * hdrs)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("git_repo", (method_id_t)git_repo_git_repo_list_all);
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list_all: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list_all: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->peer) {
        if (peer_channel_is_msgpack(_s->peer)) {
            struct picomesh_json_result _mret;
            uint8_t *_margs = malloc(16384);
            uint8_t *_mresp = malloc(65539);
            if (!_margs || !_mresp) {
                free(_margs); free(_mresp);
                return PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list_all: out of memory");
            }
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, 16384);
            cmp_write_array(&_maw, 0u);
            size_t _mrlen = 0;
            char _merr[8192] = {0};
            if (!peer_channel_msgpack_call(_s->peer, "git_repo.git_repo.list_all", hdrs,
                                           _margs, _mab.offset, _mresp, 65539,
                                           &_mrlen, _merr, sizeof(_merr))) {
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED(picomesh_json, strdup(_merr))
                            : PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list_all: msgpack call failed");
            } else {
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
            uint32_t _mssz = 0;
            if (!cmp_read_str_size(&_mrr, &_mssz)) {
                _mret = PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list_all: bad msgpack string result");
            } else if (_mrb.offset + _mssz > _mrlen) {
                _mret = PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list_all: truncated msgpack string");
            } else {
                char *_msv = malloc((size_t)_mssz + 1);
                if (!_msv) _mret = PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list_all: out of memory");
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
            return PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list_all: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list_all: remote id unresolved");
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
            return PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list_all: wire scratch alloc failed");
        }
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, hdrs, "rpc.git_repo_git_repo_list_all");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, hdrs, _a, _acap);
            if (_hn == 0) {
                ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_list_all: header serialize overflow");
                _ret = PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list_all: header serialize overflow");
                goto _rpc_done;
            }
            _off = _hn;
        }
        {
            uint64_t _h = *(uint64_t *)((char *)obj + sizeof(*obj));
            if (_off + 8 > _acap)
                { ytelemetry_span_end(&_tsp, 0, "git_repo_git_repo_list_all: pack overflow"); _ret = PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list_all: pack overflow"); goto _rpc_done; }
            memcpy(_a + _off, &_h, 8); _off += 8;
        }
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, 65536);
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
        if (_wn < 1) { _ret = PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list_all: short RPC response"); goto _rpc_done; }
        if (_wbuf[0] != 0) {
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[8193];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED(picomesh_json, strdup(_msg))
                       : PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list_all: remote error (no msg)");
            goto _rpc_done;
        }
        if (_wn < 5) { _ret = PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list_all: truncated string response"); goto _rpc_done; }
        uint32_t _slen;
        memcpy(&_slen, _wbuf + 1, 4);
        if (_wn < (size_t)5 + _slen) { _ret = PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list_all: truncated string payload"); goto _rpc_done; }
        char *_sv = malloc((size_t)_slen + 1);
        if (!_sv) { _ret = PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list_all: out of memory"); goto _rpc_done; }
        if (_slen) memcpy(_sv, _wbuf + 5, _slen);
        _sv[_slen] = 0;
        _ret = PICOMESH_OK(picomesh_json, _sv); goto _rpc_done;
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return PICOMESH_ERR(picomesh_json, "git_repo_git_repo_list_all: no impl on this class");
        return ((git_repo_git_repo_list_all_fn)fn)(ctx, obj, hdrs);
    }
}

