/* GENERATED — do not edit. */
#include <picomesh/picoclass/rpc.h>
#include <picomesh/picoclass/jinvoke.h>
#include <picomesh/picoclass/minvoke.h>
#include <picomesh/picoclass/yheaders.h>
#include <picomesh/json/json.h>
#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>
#include <picomesh/core/yspan.h>
#include <picomesh/core/ytelemetry.h>
#include <picomesh/picoclass/class.h>
#include "portalloc.internal.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct picomesh_size_result portalloc_portalloc_allocate_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.portalloc_portalloc_allocate");
    struct picomesh_uint32_result _r = portalloc_portalloc_allocate(&_local, _obj, _hdrs, _s1, _s2);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "portalloc_portalloc_allocate_skel: response buffer too small");
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
            return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
        }
        ((uint8_t *)_resp)[0] = 1;
        memcpy((uint8_t *)_resp + 1, &_ml, 4);
        memcpy((uint8_t *)_resp + 5, _msg, _ml);
        picomesh_error_destroy(_r.error);
        return PICOMESH_OK(picomesh_size, (size_t)(1 + 4 + _ml));
    }
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "portalloc_portalloc_allocate_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result portalloc_portalloc_release_skel(const void *_body, size_t _body_len,
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
    uint32_t _v1 = 0;
    if (_off + sizeof(_v1) > _body_len) goto _short_body;
    memcpy(&_v1, (const uint8_t *)_body + _off, sizeof(_v1));
    _off += sizeof(_v1);
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.portalloc_portalloc_release");
    struct picomesh_int_result _r = portalloc_portalloc_release(&_local, _obj, _hdrs, _v1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "portalloc_portalloc_release_skel: response buffer too small");
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
            return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
        }
        ((uint8_t *)_resp)[0] = 1;
        memcpy((uint8_t *)_resp + 1, &_ml, 4);
        memcpy((uint8_t *)_resp + 5, _msg, _ml);
        picomesh_error_destroy(_r.error);
        return PICOMESH_OK(picomesh_size, (size_t)(1 + 4 + _ml));
    }
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "portalloc_portalloc_release_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result portalloc_portalloc_count_used_skel(const void *_body, size_t _body_len,
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
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.portalloc_portalloc_count_used");
    struct picomesh_size_result _r = portalloc_portalloc_count_used(&_local, _obj, _hdrs);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "portalloc_portalloc_count_used_skel: response buffer too small");
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
            return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
        }
        ((uint8_t *)_resp)[0] = 1;
        memcpy((uint8_t *)_resp + 1, &_ml, 4);
        memcpy((uint8_t *)_resp + 5, _msg, _ml);
        picomesh_error_destroy(_r.error);
        return PICOMESH_OK(picomesh_size, (size_t)(1 + 4 + _ml));
    }
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "portalloc_portalloc_count_used_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result portalloc_portalloc_list_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.portalloc_portalloc_list");
    struct picomesh_json_result _r = portalloc_portalloc_list(&_local, _obj, _hdrs, _v1, _v2);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "portalloc_portalloc_list_skel: response buffer too small");
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
            return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
        }
        ((uint8_t *)_resp)[0] = 1;
        memcpy((uint8_t *)_resp + 1, &_ml, 4);
        memcpy((uint8_t *)_resp + 5, _msg, _ml);
        picomesh_error_destroy(_r.error);
        return PICOMESH_OK(picomesh_size, (size_t)(1 + 4 + _ml));
    }
    {
        const char *_sv = _r.value ? _r.value : "";
        uint32_t _svlen = (uint32_t)strlen(_sv);
        if (_resp_max < 1 + 4 + (size_t)_svlen) { free(_r.value); return PICOMESH_ERR(picomesh_size, "portalloc_portalloc_list_skel: response buffer too small"); }
        ((uint8_t *)_resp)[0] = 0;
        memcpy((uint8_t *)_resp + 1, &_svlen, 4);
        if (_svlen) memcpy((uint8_t *)_resp + 5, _sv, _svlen);
        free(_r.value);
        return PICOMESH_OK(picomesh_size, (size_t)(1 + 4 + (size_t)_svlen));
    }
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result portalloc_portalloc_list_all_skel(const void *_body, size_t _body_len,
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
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.portalloc_portalloc_list_all");
    struct picomesh_json_result _r = portalloc_portalloc_list_all(&_local, _obj, _hdrs);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "portalloc_portalloc_list_all_skel: response buffer too small");
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
            return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
        }
        ((uint8_t *)_resp)[0] = 1;
        memcpy((uint8_t *)_resp + 1, &_ml, 4);
        memcpy((uint8_t *)_resp + 5, _msg, _ml);
        picomesh_error_destroy(_r.error);
        return PICOMESH_OK(picomesh_size, (size_t)(1 + 4 + _ml));
    }
    {
        const char *_sv = _r.value ? _r.value : "";
        uint32_t _svlen = (uint32_t)strlen(_sv);
        if (_resp_max < 1 + 4 + (size_t)_svlen) { free(_r.value); return PICOMESH_ERR(picomesh_size, "portalloc_portalloc_list_all_skel: response buffer too small"); }
        ((uint8_t *)_resp)[0] = 0;
        memcpy((uint8_t *)_resp + 1, &_svlen, 4);
        if (_svlen) memcpy((uint8_t *)_resp + 5, _sv, _svlen);
        free(_r.value);
        return PICOMESH_OK(picomesh_size, (size_t)(1 + 4 + (size_t)_svlen));
    }
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_void_result portalloc_portalloc_allocate_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] portalloc_portalloc_allocate");
    const char *arg0 = json_as_string(json_array_at(args, 0), "");
    const char *arg1 = json_as_string(json_array_at(args, 1), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_uint32_result call_result = portalloc_portalloc_allocate(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "portalloc_portalloc_allocate",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "portalloc_portalloc_allocate", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result portalloc_portalloc_release_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] portalloc_portalloc_release");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = portalloc_portalloc_release(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "portalloc_portalloc_release",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "portalloc_portalloc_release", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result portalloc_portalloc_count_used_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] portalloc_portalloc_count_used");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = portalloc_portalloc_count_used(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "portalloc_portalloc_count_used",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "portalloc_portalloc_count_used", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result portalloc_portalloc_list_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] portalloc_portalloc_list");
    int64_t arg0 = (int64_t)json_as_int(json_array_at(args, 0), 0);
    int64_t arg1 = (int64_t)json_as_int(json_array_at(args, 1), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = portalloc_portalloc_list(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "portalloc_portalloc_list",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "portalloc_portalloc_list", call_result);
    }
    json_writer_raw(result, call_result.value ? call_result.value : "null");
    free(call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result portalloc_portalloc_list_all_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] portalloc_portalloc_list_all");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = portalloc_portalloc_list_all(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "portalloc_portalloc_list_all",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "portalloc_portalloc_list_all", call_result);
    }
    json_writer_raw(result, call_result.value ? call_result.value : "null");
    free(call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result portalloc_portalloc_allocate_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "portalloc_portalloc_allocate: expected 2 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "portalloc_portalloc_allocate: wrong argument count");
    }
    char _v0[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v0);
        if (!cmp_read_str(_mr, _v0, &_sz)) {
            snprintf(_err, _err_cap, "service_name: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    char _v1[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v1);
        if (!cmp_read_str(_mr, _v1, &_sz)) {
            snprintf(_err, _err_cap, "host: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_uint32_result call_result = portalloc_portalloc_allocate(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "portalloc_portalloc_allocate",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "portalloc_portalloc_allocate", call_result);
    }
    cmp_write_uinteger(_mw, (uint64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result portalloc_portalloc_release_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "portalloc_portalloc_release: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "portalloc_portalloc_release: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "port: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "port: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = portalloc_portalloc_release(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "portalloc_portalloc_release",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "portalloc_portalloc_release", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result portalloc_portalloc_count_used_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 0u) {
        snprintf(_err, _err_cap, "portalloc_portalloc_count_used: expected 0 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "portalloc_portalloc_count_used: wrong argument count");
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = portalloc_portalloc_count_used(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "portalloc_portalloc_count_used",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "portalloc_portalloc_count_used", call_result);
    }
    cmp_write_uinteger(_mw, (uint64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result portalloc_portalloc_list_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "portalloc_portalloc_list: expected 2 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "portalloc_portalloc_list: wrong argument count");
    }
    int64_t _v0;
    if (!cmp_read_integer(_mr, &_v0)) { snprintf(_err, _err_cap, "offset: expected int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
    int64_t _v1;
    if (!cmp_read_integer(_mr, &_v1)) { snprintf(_err, _err_cap, "limit: expected int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = portalloc_portalloc_list(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "portalloc_portalloc_list",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "portalloc_portalloc_list", call_result);
    }
    {
        const char *_sv = call_result.value ? call_result.value : "";
        cmp_write_str(_mw, _sv, (uint32_t)strlen(_sv));
        free(call_result.value);
    }
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result portalloc_portalloc_list_all_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 0u) {
        snprintf(_err, _err_cap, "portalloc_portalloc_list_all: expected 0 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "portalloc_portalloc_list_all: wrong argument count");
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = portalloc_portalloc_list_all(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "portalloc_portalloc_list_all",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "portalloc_portalloc_list_all", call_result);
    }
    {
        const char *_sv = call_result.value ? call_result.value : "";
        cmp_write_str(_mw, _sv, (uint32_t)strlen(_sv));
        free(call_result.value);
    }
    return PICOMESH_OK_VOID();
}

struct object_ptr_result portalloc_portalloc_create(struct ctx *ctx)
{
    ydebug("class=portalloc_portalloc");
    struct class_ptr_result _kr = portalloc_portalloc_class_get();
    if (PICOMESH_IS_ERR(_kr))
        return PICOMESH_ERR(object_ptr, "portalloc_portalloc_create: class accessor failed", _kr);
    /* A service dependency is acquired once and cached for the connection
     * (remote) / process (in-process) lifetime — no per-call create. */
    return rpc_object_acquire(ctx, _kr.value, "portalloc_portalloc");
}


/* ---- portalloc: jinvoke table ------------------------------------ */

struct portalloc_jinvoke_row { const char *name; jinvoke_fn fn; };

static const struct portalloc_jinvoke_row portalloc_jinvoke_rows[] = {
    {"portalloc_portalloc_allocate", portalloc_portalloc_allocate_jinvoke},
    {"portalloc_portalloc_release", portalloc_portalloc_release_jinvoke},
    {"portalloc_portalloc_count_used", portalloc_portalloc_count_used_jinvoke},
    {"portalloc_portalloc_list", portalloc_portalloc_list_jinvoke},
    {"portalloc_portalloc_list_all", portalloc_portalloc_list_all_jinvoke}
};

static jinvoke_fn portalloc_jinvoke_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(portalloc_jinvoke_rows) / sizeof(portalloc_jinvoke_rows[0]); ++i)
        if (strcmp(portalloc_jinvoke_rows[i].name, qname) == 0)
            return portalloc_jinvoke_rows[i].fn;
    return NULL;
}

/* ---- portalloc: minvoke table ------------------------------------ */

struct portalloc_minvoke_row { const char *name; minvoke_fn fn; };

static const struct portalloc_minvoke_row portalloc_minvoke_rows[] = {
    {"portalloc_portalloc_allocate", portalloc_portalloc_allocate_minvoke},
    {"portalloc_portalloc_release", portalloc_portalloc_release_minvoke},
    {"portalloc_portalloc_count_used", portalloc_portalloc_count_used_minvoke},
    {"portalloc_portalloc_list", portalloc_portalloc_list_minvoke},
    {"portalloc_portalloc_list_all", portalloc_portalloc_list_all_minvoke}
};

static minvoke_fn portalloc_minvoke_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(portalloc_minvoke_rows) / sizeof(portalloc_minvoke_rows[0]); ++i)
        if (strcmp(portalloc_minvoke_rows[i].name, qname) == 0)
            return portalloc_minvoke_rows[i].fn;
    return NULL;
}

/* ---- portalloc: per-method parameter signatures (runtime reflection) -- */

static const struct jinvoke_param portalloc_portalloc_allocate_params[] = {
    {"service_name", "const char *"},
    {"host", "const char *"}
};
static const struct jinvoke_param portalloc_portalloc_release_params[] = {
    {"port", "uint32_t"}
};
static const struct jinvoke_param portalloc_portalloc_list_params[] = {
    {"offset", "int64_t"},
    {"limit", "int64_t"}
};
struct portalloc_params_row { const char *name; struct jinvoke_params params; };

static const struct portalloc_params_row portalloc_params_rows[] = {
    {"portalloc_portalloc_allocate", {portalloc_portalloc_allocate_params, 2}},
    {"portalloc_portalloc_release", {portalloc_portalloc_release_params, 1}},
    {"portalloc_portalloc_count_used", {NULL, 0}},
    {"portalloc_portalloc_list", {portalloc_portalloc_list_params, 2}},
    {"portalloc_portalloc_list_all", {NULL, 0}}
};

static const struct jinvoke_params *portalloc_params_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(portalloc_params_rows) / sizeof(portalloc_params_rows[0]); ++i)
        if (strcmp(portalloc_params_rows[i].name, qname) == 0)
            return &portalloc_params_rows[i].params;
    return NULL;
}
/* ---- portalloc: class name → accessor (lazy) ---------------------- */

static struct class_ptr_result portalloc_accessor_lookup(const char *name)
{
    if (strcmp(name, "portalloc_portalloc") == 0) return portalloc_portalloc_class_get();
    return PICOMESH_OK(class_ptr, NULL);
}

/* ---- portalloc: slot → skel, name-keyed static data --------------- */

struct portalloc_skel_row { const char *name; rpc_skel_fn fn; };

static const struct portalloc_skel_row portalloc_skel_rows[] = {
    {"portalloc_portalloc_allocate", portalloc_portalloc_allocate_skel},
    {"portalloc_portalloc_release", portalloc_portalloc_release_skel},
    {"portalloc_portalloc_count_used", portalloc_portalloc_count_used_skel},
    {"portalloc_portalloc_list", portalloc_portalloc_list_skel},
    {"portalloc_portalloc_list_all", portalloc_portalloc_list_all_skel}
};

static rpc_skel_fn portalloc_skel_lookup(const char *name)
{
    /* rpc_skel_for has already resolved the slot to its qname (the only
     * Result-returning step), so this hook is a pure name→fn lookup that
     * never has to swallow an error. */
    for (size_t i = 0; i < sizeof(portalloc_skel_rows) / sizeof(portalloc_skel_rows[0]); ++i)
        if (strcmp(portalloc_skel_rows[i].name, name) == 0)
            return portalloc_skel_rows[i].fn;
    return NULL;
}

/* ---- portalloc: registration entry point (called from the driver for
 *      config-ACTIVATED plugins only — registration is activation) ---- */

struct picomesh_void_result picomesh_plugin_portalloc_register(void)
{
    struct picomesh_void_result _ar = class_add_accessor_lookup(portalloc_accessor_lookup);
    PICOMESH_RETURN_IF_ERR(picomesh_void, _ar,
                           "picomesh_plugin_portalloc_register: add accessor lookup");
    rpc_add_skel_lookup(portalloc_skel_lookup);
    jinvoke_add_lookup(portalloc_jinvoke_lookup);
    minvoke_add_lookup(portalloc_minvoke_lookup);
    jinvoke_params_add_lookup(portalloc_params_lookup);
    { struct class_ptr_result reg = portalloc_portalloc_class_get();
      PICOMESH_RETURN_IF_ERR(picomesh_void, reg, "portalloc register: prewarm portalloc_portalloc_class_get"); }
    return PICOMESH_OK_VOID();
}
