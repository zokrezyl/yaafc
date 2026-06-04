/* GENERATED — do not edit. */
#include <picomesh/yclass/rpc.h>
#include <picomesh/yclass/jinvoke.h>
#include <picomesh/yclass/minvoke.h>
#include <picomesh/yclass/yheaders.h>
#include <picomesh/yjson/yjson.h>
#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>
#include <picomesh/ycore/yspan.h>
#include <picomesh/ycore/ytelemetry.h>
#include <picomesh/yclass/class.h>
#include "storage.internal.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t storage_set_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    size_t _off = 0;
    struct ctx _local = {0};
    /* The framework header section is first on every CALL body — parse
     * it back into the `hdrs` argument before the packed business args. */
    struct yheaders *_hdrs = NULL;
    {
        size_t _hconsumed = 0;
        _hdrs = yheaders_parse(_body, _body_len, &_hconsumed);
        if (!_hdrs) goto _short_body;
        _off = _hconsumed;
    }
    struct object *_obj = NULL;
    {
        if (_off + 8 > _body_len) goto _short_body;
        uint64_t _h;
        memcpy(&_h, (const uint8_t *)_body + _off, 8); _off += 8;
        _obj = (struct object *)rpc_handle_resolve(_h);
    }
    char _s1[4096];
    {
        if (_off + 4 > _body_len) goto _short_body;
        uint32_t _slen;
        memcpy(&_slen, (const uint8_t *)_body + _off, 4); _off += 4;
        if (_off + _slen > _body_len) goto _short_body;
        if (_slen >= sizeof(_s1)) goto _short_body;
        if (_slen) memcpy(_s1, (const uint8_t *)_body + _off, _slen);
        _s1[_slen] = 0; _off += _slen;
    }
    char _s2[4096];
    {
        if (_off + 4 > _body_len) goto _short_body;
        uint32_t _slen;
        memcpy(&_slen, (const uint8_t *)_body + _off, 4); _off += 4;
        if (_off + _slen > _body_len) goto _short_body;
        if (_slen >= sizeof(_s2)) goto _short_body;
        if (_slen) memcpy(_s2, (const uint8_t *)_body + _off, _slen);
        _s2[_slen] = 0; _off += _slen;
    }
    char _s3[4096];
    {
        if (_off + 4 > _body_len) goto _short_body;
        uint32_t _slen;
        memcpy(&_slen, (const uint8_t *)_body + _off, 4); _off += 4;
        if (_off + _slen > _body_len) goto _short_body;
        if (_slen >= sizeof(_s3)) goto _short_body;
        if (_slen) memcpy(_s3, (const uint8_t *)_body + _off, _slen);
        _s3[_slen] = 0; _off += _slen;
    }
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.storage_set");
    struct picomesh_int_result _r = storage_set(&_local, _obj, _hdrs, _s1, _s2, _s3);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return 0;
    if (PICOMESH_IS_ERR(_r)) {
        char _errbuf[8192] = {0};
        picomesh_error_snprint(_errbuf, sizeof(_errbuf), _r.error);
        const char *_msg = _errbuf[0] ? _errbuf : (_r.error.msg ? _r.error.msg : "(no msg)");
        uint32_t _ml = (uint32_t)strlen(_msg);
        if (_resp_max <= 5) _ml = 0;
        else if (_ml > _resp_max - 5) _ml = (uint32_t)(_resp_max - 5);
        if (_resp_max < 1 + 4 + _ml) {
            picomesh_error_destroy(_r.error);
            ((uint8_t *)_resp)[0] = 1;
            return _resp_max >= 1 ? 1 : 0;
        }
        ((uint8_t *)_resp)[0] = 1;
        memcpy((uint8_t *)_resp + 1, &_ml, 4);
        memcpy((uint8_t *)_resp + 5, _msg, _ml);
        picomesh_error_destroy(_r.error);
        return 1 + 4 + _ml;
    }
    if (_resp_max < 1 + sizeof(_r.value)) return 0;
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return 1 + sizeof(_r.value);
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return _resp_max >= 1 ? 1 : 0;
}

static size_t storage_get_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    size_t _off = 0;
    struct ctx _local = {0};
    /* The framework header section is first on every CALL body — parse
     * it back into the `hdrs` argument before the packed business args. */
    struct yheaders *_hdrs = NULL;
    {
        size_t _hconsumed = 0;
        _hdrs = yheaders_parse(_body, _body_len, &_hconsumed);
        if (!_hdrs) goto _short_body;
        _off = _hconsumed;
    }
    struct object *_obj = NULL;
    {
        if (_off + 8 > _body_len) goto _short_body;
        uint64_t _h;
        memcpy(&_h, (const uint8_t *)_body + _off, 8); _off += 8;
        _obj = (struct object *)rpc_handle_resolve(_h);
    }
    char _s1[4096];
    {
        if (_off + 4 > _body_len) goto _short_body;
        uint32_t _slen;
        memcpy(&_slen, (const uint8_t *)_body + _off, 4); _off += 4;
        if (_off + _slen > _body_len) goto _short_body;
        if (_slen >= sizeof(_s1)) goto _short_body;
        if (_slen) memcpy(_s1, (const uint8_t *)_body + _off, _slen);
        _s1[_slen] = 0; _off += _slen;
    }
    char _s2[4096];
    {
        if (_off + 4 > _body_len) goto _short_body;
        uint32_t _slen;
        memcpy(&_slen, (const uint8_t *)_body + _off, 4); _off += 4;
        if (_off + _slen > _body_len) goto _short_body;
        if (_slen >= sizeof(_s2)) goto _short_body;
        if (_slen) memcpy(_s2, (const uint8_t *)_body + _off, _slen);
        _s2[_slen] = 0; _off += _slen;
    }
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.storage_get");
    struct picomesh_string_result _r = storage_get(&_local, _obj, _hdrs, _s1, _s2);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return 0;
    if (PICOMESH_IS_ERR(_r)) {
        char _errbuf[8192] = {0};
        picomesh_error_snprint(_errbuf, sizeof(_errbuf), _r.error);
        const char *_msg = _errbuf[0] ? _errbuf : (_r.error.msg ? _r.error.msg : "(no msg)");
        uint32_t _ml = (uint32_t)strlen(_msg);
        if (_resp_max <= 5) _ml = 0;
        else if (_ml > _resp_max - 5) _ml = (uint32_t)(_resp_max - 5);
        if (_resp_max < 1 + 4 + _ml) {
            picomesh_error_destroy(_r.error);
            ((uint8_t *)_resp)[0] = 1;
            return _resp_max >= 1 ? 1 : 0;
        }
        ((uint8_t *)_resp)[0] = 1;
        memcpy((uint8_t *)_resp + 1, &_ml, 4);
        memcpy((uint8_t *)_resp + 5, _msg, _ml);
        picomesh_error_destroy(_r.error);
        return 1 + 4 + _ml;
    }
    {
        const char *_sv = _r.value ? _r.value : "";
        uint32_t _svlen = (uint32_t)strlen(_sv);
        if (_resp_max < 1 + 4 + (size_t)_svlen) { free(_r.value); return 0; }
        ((uint8_t *)_resp)[0] = 0;
        memcpy((uint8_t *)_resp + 1, &_svlen, 4);
        if (_svlen) memcpy((uint8_t *)_resp + 5, _sv, _svlen);
        free(_r.value);
        return 1 + 4 + (size_t)_svlen;
    }
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return _resp_max >= 1 ? 1 : 0;
}

static size_t storage_exists_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    size_t _off = 0;
    struct ctx _local = {0};
    /* The framework header section is first on every CALL body — parse
     * it back into the `hdrs` argument before the packed business args. */
    struct yheaders *_hdrs = NULL;
    {
        size_t _hconsumed = 0;
        _hdrs = yheaders_parse(_body, _body_len, &_hconsumed);
        if (!_hdrs) goto _short_body;
        _off = _hconsumed;
    }
    struct object *_obj = NULL;
    {
        if (_off + 8 > _body_len) goto _short_body;
        uint64_t _h;
        memcpy(&_h, (const uint8_t *)_body + _off, 8); _off += 8;
        _obj = (struct object *)rpc_handle_resolve(_h);
    }
    char _s1[4096];
    {
        if (_off + 4 > _body_len) goto _short_body;
        uint32_t _slen;
        memcpy(&_slen, (const uint8_t *)_body + _off, 4); _off += 4;
        if (_off + _slen > _body_len) goto _short_body;
        if (_slen >= sizeof(_s1)) goto _short_body;
        if (_slen) memcpy(_s1, (const uint8_t *)_body + _off, _slen);
        _s1[_slen] = 0; _off += _slen;
    }
    char _s2[4096];
    {
        if (_off + 4 > _body_len) goto _short_body;
        uint32_t _slen;
        memcpy(&_slen, (const uint8_t *)_body + _off, 4); _off += 4;
        if (_off + _slen > _body_len) goto _short_body;
        if (_slen >= sizeof(_s2)) goto _short_body;
        if (_slen) memcpy(_s2, (const uint8_t *)_body + _off, _slen);
        _s2[_slen] = 0; _off += _slen;
    }
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.storage_exists");
    struct picomesh_int_result _r = storage_exists(&_local, _obj, _hdrs, _s1, _s2);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return 0;
    if (PICOMESH_IS_ERR(_r)) {
        char _errbuf[8192] = {0};
        picomesh_error_snprint(_errbuf, sizeof(_errbuf), _r.error);
        const char *_msg = _errbuf[0] ? _errbuf : (_r.error.msg ? _r.error.msg : "(no msg)");
        uint32_t _ml = (uint32_t)strlen(_msg);
        if (_resp_max <= 5) _ml = 0;
        else if (_ml > _resp_max - 5) _ml = (uint32_t)(_resp_max - 5);
        if (_resp_max < 1 + 4 + _ml) {
            picomesh_error_destroy(_r.error);
            ((uint8_t *)_resp)[0] = 1;
            return _resp_max >= 1 ? 1 : 0;
        }
        ((uint8_t *)_resp)[0] = 1;
        memcpy((uint8_t *)_resp + 1, &_ml, 4);
        memcpy((uint8_t *)_resp + 5, _msg, _ml);
        picomesh_error_destroy(_r.error);
        return 1 + 4 + _ml;
    }
    if (_resp_max < 1 + sizeof(_r.value)) return 0;
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return 1 + sizeof(_r.value);
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return _resp_max >= 1 ? 1 : 0;
}

static size_t storage_del_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    size_t _off = 0;
    struct ctx _local = {0};
    /* The framework header section is first on every CALL body — parse
     * it back into the `hdrs` argument before the packed business args. */
    struct yheaders *_hdrs = NULL;
    {
        size_t _hconsumed = 0;
        _hdrs = yheaders_parse(_body, _body_len, &_hconsumed);
        if (!_hdrs) goto _short_body;
        _off = _hconsumed;
    }
    struct object *_obj = NULL;
    {
        if (_off + 8 > _body_len) goto _short_body;
        uint64_t _h;
        memcpy(&_h, (const uint8_t *)_body + _off, 8); _off += 8;
        _obj = (struct object *)rpc_handle_resolve(_h);
    }
    char _s1[4096];
    {
        if (_off + 4 > _body_len) goto _short_body;
        uint32_t _slen;
        memcpy(&_slen, (const uint8_t *)_body + _off, 4); _off += 4;
        if (_off + _slen > _body_len) goto _short_body;
        if (_slen >= sizeof(_s1)) goto _short_body;
        if (_slen) memcpy(_s1, (const uint8_t *)_body + _off, _slen);
        _s1[_slen] = 0; _off += _slen;
    }
    char _s2[4096];
    {
        if (_off + 4 > _body_len) goto _short_body;
        uint32_t _slen;
        memcpy(&_slen, (const uint8_t *)_body + _off, 4); _off += 4;
        if (_off + _slen > _body_len) goto _short_body;
        if (_slen >= sizeof(_s2)) goto _short_body;
        if (_slen) memcpy(_s2, (const uint8_t *)_body + _off, _slen);
        _s2[_slen] = 0; _off += _slen;
    }
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.storage_del");
    struct picomesh_int_result _r = storage_del(&_local, _obj, _hdrs, _s1, _s2);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return 0;
    if (PICOMESH_IS_ERR(_r)) {
        char _errbuf[8192] = {0};
        picomesh_error_snprint(_errbuf, sizeof(_errbuf), _r.error);
        const char *_msg = _errbuf[0] ? _errbuf : (_r.error.msg ? _r.error.msg : "(no msg)");
        uint32_t _ml = (uint32_t)strlen(_msg);
        if (_resp_max <= 5) _ml = 0;
        else if (_ml > _resp_max - 5) _ml = (uint32_t)(_resp_max - 5);
        if (_resp_max < 1 + 4 + _ml) {
            picomesh_error_destroy(_r.error);
            ((uint8_t *)_resp)[0] = 1;
            return _resp_max >= 1 ? 1 : 0;
        }
        ((uint8_t *)_resp)[0] = 1;
        memcpy((uint8_t *)_resp + 1, &_ml, 4);
        memcpy((uint8_t *)_resp + 5, _msg, _ml);
        picomesh_error_destroy(_r.error);
        return 1 + 4 + _ml;
    }
    if (_resp_max < 1 + sizeof(_r.value)) return 0;
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return 1 + sizeof(_r.value);
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return _resp_max >= 1 ? 1 : 0;
}

static size_t storage_count_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    size_t _off = 0;
    struct ctx _local = {0};
    /* The framework header section is first on every CALL body — parse
     * it back into the `hdrs` argument before the packed business args. */
    struct yheaders *_hdrs = NULL;
    {
        size_t _hconsumed = 0;
        _hdrs = yheaders_parse(_body, _body_len, &_hconsumed);
        if (!_hdrs) goto _short_body;
        _off = _hconsumed;
    }
    struct object *_obj = NULL;
    {
        if (_off + 8 > _body_len) goto _short_body;
        uint64_t _h;
        memcpy(&_h, (const uint8_t *)_body + _off, 8); _off += 8;
        _obj = (struct object *)rpc_handle_resolve(_h);
    }
    char _s1[4096];
    {
        if (_off + 4 > _body_len) goto _short_body;
        uint32_t _slen;
        memcpy(&_slen, (const uint8_t *)_body + _off, 4); _off += 4;
        if (_off + _slen > _body_len) goto _short_body;
        if (_slen >= sizeof(_s1)) goto _short_body;
        if (_slen) memcpy(_s1, (const uint8_t *)_body + _off, _slen);
        _s1[_slen] = 0; _off += _slen;
    }
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.storage_count");
    struct picomesh_size_result _r = storage_count(&_local, _obj, _hdrs, _s1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return 0;
    if (PICOMESH_IS_ERR(_r)) {
        char _errbuf[8192] = {0};
        picomesh_error_snprint(_errbuf, sizeof(_errbuf), _r.error);
        const char *_msg = _errbuf[0] ? _errbuf : (_r.error.msg ? _r.error.msg : "(no msg)");
        uint32_t _ml = (uint32_t)strlen(_msg);
        if (_resp_max <= 5) _ml = 0;
        else if (_ml > _resp_max - 5) _ml = (uint32_t)(_resp_max - 5);
        if (_resp_max < 1 + 4 + _ml) {
            picomesh_error_destroy(_r.error);
            ((uint8_t *)_resp)[0] = 1;
            return _resp_max >= 1 ? 1 : 0;
        }
        ((uint8_t *)_resp)[0] = 1;
        memcpy((uint8_t *)_resp + 1, &_ml, 4);
        memcpy((uint8_t *)_resp + 5, _msg, _ml);
        picomesh_error_destroy(_r.error);
        return 1 + 4 + _ml;
    }
    if (_resp_max < 1 + sizeof(_r.value)) return 0;
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return 1 + sizeof(_r.value);
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return _resp_max >= 1 ? 1 : 0;
}

static int storage_set_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    const char *arg0 = yjson_as_string(yjson_array_at(args, 0), "");
    const char *arg1 = yjson_as_string(yjson_array_at(args, 1), "");
    const char *arg2 = yjson_as_string(yjson_array_at(args, 2), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = storage_set(call_ctx, obj, hdrs, arg0, arg1, arg2);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "storage_set",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int storage_get_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    const char *arg0 = yjson_as_string(yjson_array_at(args, 0), "");
    const char *arg1 = yjson_as_string(yjson_array_at(args, 1), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_string_result call_result = storage_get(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "storage_get",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_string(result, call_result.value ? call_result.value : "");
    free(call_result.value);
    return 0;
}

static int storage_exists_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    const char *arg0 = yjson_as_string(yjson_array_at(args, 0), "");
    const char *arg1 = yjson_as_string(yjson_array_at(args, 1), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = storage_exists(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "storage_exists",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int storage_del_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    const char *arg0 = yjson_as_string(yjson_array_at(args, 0), "");
    const char *arg1 = yjson_as_string(yjson_array_at(args, 1), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = storage_del(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "storage_del",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int storage_count_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    const char *arg0 = yjson_as_string(yjson_array_at(args, 0), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = storage_count(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "storage_count",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int storage_set_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 3u) {
        snprintf(_err, _err_cap, "storage_set: expected 3 arg(s), got %u", _argc);
        return -1;
    }
    char _v0[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v0);
        if (!cmp_read_str(_mr, _v0, &_sz)) {
            snprintf(_err, _err_cap, "context: expected str arg (%s)", cmp_strerror(_mr));
            return -1;
        }
    }
    char _v1[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v1);
        if (!cmp_read_str(_mr, _v1, &_sz)) {
            snprintf(_err, _err_cap, "key: expected str arg (%s)", cmp_strerror(_mr));
            return -1;
        }
    }
    char _v2[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v2);
        if (!cmp_read_str(_mr, _v2, &_sz)) {
            snprintf(_err, _err_cap, "value: expected str arg (%s)", cmp_strerror(_mr));
            return -1;
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = storage_set(call_ctx, obj, hdrs, _v0, _v1, _v2);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "storage_set",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return 0;
}

static int storage_get_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "storage_get: expected 2 arg(s), got %u", _argc);
        return -1;
    }
    char _v0[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v0);
        if (!cmp_read_str(_mr, _v0, &_sz)) {
            snprintf(_err, _err_cap, "context: expected str arg (%s)", cmp_strerror(_mr));
            return -1;
        }
    }
    char _v1[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v1);
        if (!cmp_read_str(_mr, _v1, &_sz)) {
            snprintf(_err, _err_cap, "key: expected str arg (%s)", cmp_strerror(_mr));
            return -1;
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_string_result call_result = storage_get(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "storage_get",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    {
        const char *_sv = call_result.value ? call_result.value : "";
        cmp_write_str(_mw, _sv, (uint32_t)strlen(_sv));
        free(call_result.value);
    }
    return 0;
}

static int storage_exists_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "storage_exists: expected 2 arg(s), got %u", _argc);
        return -1;
    }
    char _v0[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v0);
        if (!cmp_read_str(_mr, _v0, &_sz)) {
            snprintf(_err, _err_cap, "context: expected str arg (%s)", cmp_strerror(_mr));
            return -1;
        }
    }
    char _v1[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v1);
        if (!cmp_read_str(_mr, _v1, &_sz)) {
            snprintf(_err, _err_cap, "key: expected str arg (%s)", cmp_strerror(_mr));
            return -1;
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = storage_exists(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "storage_exists",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return 0;
}

static int storage_del_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "storage_del: expected 2 arg(s), got %u", _argc);
        return -1;
    }
    char _v0[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v0);
        if (!cmp_read_str(_mr, _v0, &_sz)) {
            snprintf(_err, _err_cap, "context: expected str arg (%s)", cmp_strerror(_mr));
            return -1;
        }
    }
    char _v1[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v1);
        if (!cmp_read_str(_mr, _v1, &_sz)) {
            snprintf(_err, _err_cap, "key: expected str arg (%s)", cmp_strerror(_mr));
            return -1;
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = storage_del(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "storage_del",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return 0;
}

static int storage_count_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "storage_count: expected 1 arg(s), got %u", _argc);
        return -1;
    }
    char _v0[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v0);
        if (!cmp_read_str(_mr, _v0, &_sz)) {
            snprintf(_err, _err_cap, "context: expected str arg (%s)", cmp_strerror(_mr));
            return -1;
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = storage_count(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "storage_count",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_uinteger(_mw, (uint64_t)call_result.value);
    return 0;
}

struct object_ptr_result storage_db_create(struct ctx *ctx)
{
    ydebug("class=storage_db");
    struct class_ptr_result _kr = storage_db_class_get();
    if (PICOMESH_IS_ERR(_kr))
        return PICOMESH_ERR(object_ptr, "storage_db_create: class accessor failed", _kr);
    /* A service dependency is acquired once and cached for the connection
     * (remote) / process (in-process) lifetime — no per-call create. */
    return rpc_object_acquire(ctx, _kr.value, "storage_db");
}


/* ---- storage: jinvoke table ------------------------------------ */

struct storage_jinvoke_row { const char *name; jinvoke_fn fn; };

static const struct storage_jinvoke_row storage_jinvoke_rows[] = {
    {"storage_set", storage_set_jinvoke},
    {"storage_get", storage_get_jinvoke},
    {"storage_exists", storage_exists_jinvoke},
    {"storage_del", storage_del_jinvoke},
    {"storage_count", storage_count_jinvoke}
};

static jinvoke_fn storage_jinvoke_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(storage_jinvoke_rows) / sizeof(storage_jinvoke_rows[0]); ++i)
        if (strcmp(storage_jinvoke_rows[i].name, qname) == 0)
            return storage_jinvoke_rows[i].fn;
    return NULL;
}

/* ---- storage: minvoke table ------------------------------------ */

struct storage_minvoke_row { const char *name; minvoke_fn fn; };

static const struct storage_minvoke_row storage_minvoke_rows[] = {
    {"storage_set", storage_set_minvoke},
    {"storage_get", storage_get_minvoke},
    {"storage_exists", storage_exists_minvoke},
    {"storage_del", storage_del_minvoke},
    {"storage_count", storage_count_minvoke}
};

static minvoke_fn storage_minvoke_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(storage_minvoke_rows) / sizeof(storage_minvoke_rows[0]); ++i)
        if (strcmp(storage_minvoke_rows[i].name, qname) == 0)
            return storage_minvoke_rows[i].fn;
    return NULL;
}

/* ---- storage: per-method parameter signatures (runtime reflection) -- */

static const struct jinvoke_param storage_set_params[] = {
    {"context", "const char *"},
    {"key", "const char *"},
    {"value", "const char *"}
};
static const struct jinvoke_param storage_get_params[] = {
    {"context", "const char *"},
    {"key", "const char *"}
};
static const struct jinvoke_param storage_exists_params[] = {
    {"context", "const char *"},
    {"key", "const char *"}
};
static const struct jinvoke_param storage_del_params[] = {
    {"context", "const char *"},
    {"key", "const char *"}
};
static const struct jinvoke_param storage_count_params[] = {
    {"context", "const char *"}
};
struct storage_params_row { const char *name; struct jinvoke_params params; };

static const struct storage_params_row storage_params_rows[] = {
    {"storage_set", {storage_set_params, 3}},
    {"storage_get", {storage_get_params, 2}},
    {"storage_exists", {storage_exists_params, 2}},
    {"storage_del", {storage_del_params, 2}},
    {"storage_count", {storage_count_params, 1}}
};

static const struct jinvoke_params *storage_params_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(storage_params_rows) / sizeof(storage_params_rows[0]); ++i)
        if (strcmp(storage_params_rows[i].name, qname) == 0)
            return &storage_params_rows[i].params;
    return NULL;
}
/* ---- storage: class name → accessor (lazy) ---------------------- */

static struct class_ptr_result storage_accessor_lookup(const char *name)
{
    if (strcmp(name, "storage_db") == 0) return storage_db_class_get();
    return PICOMESH_OK(class_ptr, NULL);
}

/* ---- storage: slot → skel, name-keyed static data --------------- */

struct storage_skel_row { const char *name; rpc_skel_fn fn; };

static const struct storage_skel_row storage_skel_rows[] = {
    {"storage_set", storage_set_skel},
    {"storage_get", storage_get_skel},
    {"storage_exists", storage_exists_skel},
    {"storage_del", storage_del_skel},
    {"storage_count", storage_count_skel}
};

static rpc_skel_fn storage_skel_lookup(method_slot slot)
{
    struct const_char_ptr_result nr = method_slot_name(slot);
    if (PICOMESH_IS_ERR(nr)) { picomesh_error_destroy(nr.error); return NULL; }
    const char *name = nr.value;
    for (size_t i = 0; i < sizeof(storage_skel_rows) / sizeof(storage_skel_rows[0]); ++i)
        if (strcmp(storage_skel_rows[i].name, name) == 0)
            return storage_skel_rows[i].fn;
    return NULL;
}

/* ---- storage: registration entry point (called from the driver for
 *      config-ACTIVATED plugins only — registration is activation) ---- */

void picomesh_plugin_storage_register(void)
{
    struct picomesh_void_result _ar = class_add_accessor_lookup(storage_accessor_lookup);
    if (PICOMESH_IS_ERR(_ar)) {
        picomesh_error_print(stderr, "picomesh_plugin_storage_register", _ar.error);
        picomesh_error_destroy(_ar.error);
        abort();
    }
    rpc_add_skel_lookup(storage_skel_lookup);
    jinvoke_add_lookup(storage_jinvoke_lookup);
    minvoke_add_lookup(storage_minvoke_lookup);
    jinvoke_params_add_lookup(storage_params_lookup);
    { struct class_ptr_result reg = storage_db_class_get();
      if (PICOMESH_IS_ERR(reg)) picomesh_error_destroy(reg.error); }
}
