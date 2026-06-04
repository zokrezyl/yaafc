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
#include "calculator.internal.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t calculator_calc_add_skel(const void *_body, size_t _body_len,
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
    int64_t _v1 = 0;
    if (_off + sizeof(_v1) > _body_len) goto _short_body;
    memcpy(&_v1, (const uint8_t *)_body + _off, sizeof(_v1));
    _off += sizeof(_v1);
    int64_t _v2 = 0;
    if (_off + sizeof(_v2) > _body_len) goto _short_body;
    memcpy(&_v2, (const uint8_t *)_body + _off, sizeof(_v2));
    _off += sizeof(_v2);
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.calculator_calc_add");
    struct picomesh_int64_result _r = calculator_calc_add(&_local, _obj, _hdrs, _v1, _v2);
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

static size_t calculator_calc_sub_skel(const void *_body, size_t _body_len,
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
    int64_t _v1 = 0;
    if (_off + sizeof(_v1) > _body_len) goto _short_body;
    memcpy(&_v1, (const uint8_t *)_body + _off, sizeof(_v1));
    _off += sizeof(_v1);
    int64_t _v2 = 0;
    if (_off + sizeof(_v2) > _body_len) goto _short_body;
    memcpy(&_v2, (const uint8_t *)_body + _off, sizeof(_v2));
    _off += sizeof(_v2);
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.calculator_calc_sub");
    struct picomesh_int64_result _r = calculator_calc_sub(&_local, _obj, _hdrs, _v1, _v2);
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

static size_t calculator_calc_mul_skel(const void *_body, size_t _body_len,
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
    int64_t _v1 = 0;
    if (_off + sizeof(_v1) > _body_len) goto _short_body;
    memcpy(&_v1, (const uint8_t *)_body + _off, sizeof(_v1));
    _off += sizeof(_v1);
    int64_t _v2 = 0;
    if (_off + sizeof(_v2) > _body_len) goto _short_body;
    memcpy(&_v2, (const uint8_t *)_body + _off, sizeof(_v2));
    _off += sizeof(_v2);
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.calculator_calc_mul");
    struct picomesh_int64_result _r = calculator_calc_mul(&_local, _obj, _hdrs, _v1, _v2);
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

static size_t calculator_calc_div_skel(const void *_body, size_t _body_len,
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
    int64_t _v1 = 0;
    if (_off + sizeof(_v1) > _body_len) goto _short_body;
    memcpy(&_v1, (const uint8_t *)_body + _off, sizeof(_v1));
    _off += sizeof(_v1);
    int64_t _v2 = 0;
    if (_off + sizeof(_v2) > _body_len) goto _short_body;
    memcpy(&_v2, (const uint8_t *)_body + _off, sizeof(_v2));
    _off += sizeof(_v2);
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.calculator_calc_div");
    struct picomesh_int64_result _r = calculator_calc_div(&_local, _obj, _hdrs, _v1, _v2);
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

static int calculator_calc_add_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    int64_t arg0 = (int64_t)yjson_as_int(yjson_array_at(args, 0), 0);
    int64_t arg1 = (int64_t)yjson_as_int(yjson_array_at(args, 1), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = calculator_calc_add(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "calculator_calc_add",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int calculator_calc_sub_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    int64_t arg0 = (int64_t)yjson_as_int(yjson_array_at(args, 0), 0);
    int64_t arg1 = (int64_t)yjson_as_int(yjson_array_at(args, 1), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = calculator_calc_sub(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "calculator_calc_sub",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int calculator_calc_mul_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    int64_t arg0 = (int64_t)yjson_as_int(yjson_array_at(args, 0), 0);
    int64_t arg1 = (int64_t)yjson_as_int(yjson_array_at(args, 1), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = calculator_calc_mul(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "calculator_calc_mul",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int calculator_calc_div_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    int64_t arg0 = (int64_t)yjson_as_int(yjson_array_at(args, 0), 0);
    int64_t arg1 = (int64_t)yjson_as_int(yjson_array_at(args, 1), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = calculator_calc_div(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "calculator_calc_div",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int calculator_calc_add_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "calculator_calc_add: expected 2 arg(s), got %u", _argc);
        return -1;
    }
    int64_t _v0;
    if (!cmp_read_integer(_mr, &_v0)) { snprintf(_err, _err_cap, "x: expected int (%s)", cmp_strerror(_mr)); return -1; }
    int64_t _v1;
    if (!cmp_read_integer(_mr, &_v1)) { snprintf(_err, _err_cap, "y: expected int (%s)", cmp_strerror(_mr)); return -1; }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = calculator_calc_add(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "calculator_calc_add",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return 0;
}

static int calculator_calc_sub_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "calculator_calc_sub: expected 2 arg(s), got %u", _argc);
        return -1;
    }
    int64_t _v0;
    if (!cmp_read_integer(_mr, &_v0)) { snprintf(_err, _err_cap, "x: expected int (%s)", cmp_strerror(_mr)); return -1; }
    int64_t _v1;
    if (!cmp_read_integer(_mr, &_v1)) { snprintf(_err, _err_cap, "y: expected int (%s)", cmp_strerror(_mr)); return -1; }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = calculator_calc_sub(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "calculator_calc_sub",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return 0;
}

static int calculator_calc_mul_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "calculator_calc_mul: expected 2 arg(s), got %u", _argc);
        return -1;
    }
    int64_t _v0;
    if (!cmp_read_integer(_mr, &_v0)) { snprintf(_err, _err_cap, "x: expected int (%s)", cmp_strerror(_mr)); return -1; }
    int64_t _v1;
    if (!cmp_read_integer(_mr, &_v1)) { snprintf(_err, _err_cap, "y: expected int (%s)", cmp_strerror(_mr)); return -1; }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = calculator_calc_mul(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "calculator_calc_mul",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return 0;
}

static int calculator_calc_div_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "calculator_calc_div: expected 2 arg(s), got %u", _argc);
        return -1;
    }
    int64_t _v0;
    if (!cmp_read_integer(_mr, &_v0)) { snprintf(_err, _err_cap, "x: expected int (%s)", cmp_strerror(_mr)); return -1; }
    int64_t _v1;
    if (!cmp_read_integer(_mr, &_v1)) { snprintf(_err, _err_cap, "y: expected int (%s)", cmp_strerror(_mr)); return -1; }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = calculator_calc_div(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "calculator_calc_div",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return 0;
}

struct object_ptr_result calculator_calc_create(struct ctx *ctx)
{
    ydebug("class=calculator_calc");
    struct class_ptr_result _kr = calculator_calc_class_get();
    if (PICOMESH_IS_ERR(_kr))
        return PICOMESH_ERR(object_ptr, "calculator_calc_create: class accessor failed", _kr);
    /* A service dependency is acquired once and cached for the connection
     * (remote) / process (in-process) lifetime — no per-call create. */
    return rpc_object_acquire(ctx, _kr.value, "calculator_calc");
}


/* ---- calculator: jinvoke table ------------------------------------ */

struct calculator_jinvoke_row { const char *name; jinvoke_fn fn; };

static const struct calculator_jinvoke_row calculator_jinvoke_rows[] = {
    {"calculator_calc_add", calculator_calc_add_jinvoke},
    {"calculator_calc_sub", calculator_calc_sub_jinvoke},
    {"calculator_calc_mul", calculator_calc_mul_jinvoke},
    {"calculator_calc_div", calculator_calc_div_jinvoke}
};

static jinvoke_fn calculator_jinvoke_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(calculator_jinvoke_rows) / sizeof(calculator_jinvoke_rows[0]); ++i)
        if (strcmp(calculator_jinvoke_rows[i].name, qname) == 0)
            return calculator_jinvoke_rows[i].fn;
    return NULL;
}

/* ---- calculator: minvoke table ------------------------------------ */

struct calculator_minvoke_row { const char *name; minvoke_fn fn; };

static const struct calculator_minvoke_row calculator_minvoke_rows[] = {
    {"calculator_calc_add", calculator_calc_add_minvoke},
    {"calculator_calc_sub", calculator_calc_sub_minvoke},
    {"calculator_calc_mul", calculator_calc_mul_minvoke},
    {"calculator_calc_div", calculator_calc_div_minvoke}
};

static minvoke_fn calculator_minvoke_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(calculator_minvoke_rows) / sizeof(calculator_minvoke_rows[0]); ++i)
        if (strcmp(calculator_minvoke_rows[i].name, qname) == 0)
            return calculator_minvoke_rows[i].fn;
    return NULL;
}

/* ---- calculator: per-method parameter signatures (runtime reflection) -- */

static const struct jinvoke_param calculator_calc_add_params[] = {
    {"x", "int64_t"},
    {"y", "int64_t"}
};
static const struct jinvoke_param calculator_calc_sub_params[] = {
    {"x", "int64_t"},
    {"y", "int64_t"}
};
static const struct jinvoke_param calculator_calc_mul_params[] = {
    {"x", "int64_t"},
    {"y", "int64_t"}
};
static const struct jinvoke_param calculator_calc_div_params[] = {
    {"x", "int64_t"},
    {"y", "int64_t"}
};
struct calculator_params_row { const char *name; struct jinvoke_params params; };

static const struct calculator_params_row calculator_params_rows[] = {
    {"calculator_calc_add", {calculator_calc_add_params, 2}},
    {"calculator_calc_sub", {calculator_calc_sub_params, 2}},
    {"calculator_calc_mul", {calculator_calc_mul_params, 2}},
    {"calculator_calc_div", {calculator_calc_div_params, 2}}
};

static const struct jinvoke_params *calculator_params_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(calculator_params_rows) / sizeof(calculator_params_rows[0]); ++i)
        if (strcmp(calculator_params_rows[i].name, qname) == 0)
            return &calculator_params_rows[i].params;
    return NULL;
}
/* ---- calculator: class name → accessor (lazy) ---------------------- */

static struct class_ptr_result calculator_accessor_lookup(const char *name)
{
    if (strcmp(name, "calculator_calc") == 0) return calculator_calc_class_get();
    return PICOMESH_OK(class_ptr, NULL);
}

/* ---- calculator: slot → skel, name-keyed static data --------------- */

struct calculator_skel_row { const char *name; rpc_skel_fn fn; };

static const struct calculator_skel_row calculator_skel_rows[] = {
    {"calculator_calc_add", calculator_calc_add_skel},
    {"calculator_calc_sub", calculator_calc_sub_skel},
    {"calculator_calc_mul", calculator_calc_mul_skel},
    {"calculator_calc_div", calculator_calc_div_skel}
};

static rpc_skel_fn calculator_skel_lookup(method_slot slot)
{
    struct const_char_ptr_result nr = method_slot_name(slot);
    if (PICOMESH_IS_ERR(nr)) { picomesh_error_destroy(nr.error); return NULL; }
    const char *name = nr.value;
    for (size_t i = 0; i < sizeof(calculator_skel_rows) / sizeof(calculator_skel_rows[0]); ++i)
        if (strcmp(calculator_skel_rows[i].name, name) == 0)
            return calculator_skel_rows[i].fn;
    return NULL;
}

/* ---- calculator: registration entry point (called from the driver for
 *      config-ACTIVATED plugins only — registration is activation) ---- */

void picomesh_plugin_calculator_register(void)
{
    struct picomesh_void_result _ar = class_add_accessor_lookup(calculator_accessor_lookup);
    if (PICOMESH_IS_ERR(_ar)) {
        picomesh_error_print(stderr, "picomesh_plugin_calculator_register", _ar.error);
        picomesh_error_destroy(_ar.error);
        abort();
    }
    rpc_add_skel_lookup(calculator_skel_lookup);
    jinvoke_add_lookup(calculator_jinvoke_lookup);
    minvoke_add_lookup(calculator_minvoke_lookup);
    jinvoke_params_add_lookup(calculator_params_lookup);
    { struct class_ptr_result reg = calculator_calc_class_get();
      if (PICOMESH_IS_ERR(reg)) picomesh_error_destroy(reg.error); }
}
