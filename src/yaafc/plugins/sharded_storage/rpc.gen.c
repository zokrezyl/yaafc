/* GENERATED — do not edit. */
#include <yaafc/yclass/rpc.h>
#include <yaafc/yclass/jinvoke.h>
#include <yaafc/yclass/yheaders.h>
#include <yaafc/yjson/yjson.h>
#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/ycore/yspan.h>
#include <yaafc/yclass/class.h>
#include "sharded_storage.internal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t sharded_storage_db_set_skel(const void *_body, size_t _body_len,
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
    double span_start = yaafc_ytime_monotonic_sec();
    struct yaafc_int_result _r = sharded_storage_db_set(&_local, _obj, _hdrs, _s1, _s2, _s3);
    {
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        const char *span_trace = _hdrs ? yheaders_get(_hdrs, "trace_id") : "-";
        ydebug("span trace=%s op=skel.sharded_storage_db_set dur_us=%.0f", span_trace ? span_trace : "-", span_us);
        yspan_record("skel.sharded_storage_db_set", span_us);
    }
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {
        yaafc_error_print(stderr, "[skel] sharded_storage_db_set", _r.error);
        const char *_msg = _r.error.msg ? _r.error.msg : "(no msg)";
        uint32_t _ml = (uint32_t)strlen(_msg);
        if (_ml > 256) _ml = 256;
        if (_resp_max < 1 + 4 + _ml) {
            yaafc_error_destroy(_r.error);
            ((uint8_t *)_resp)[0] = 1;
            return _resp_max >= 1 ? 1 : 0;
        }
        ((uint8_t *)_resp)[0] = 1;
        memcpy((uint8_t *)_resp + 1, &_ml, 4);
        memcpy((uint8_t *)_resp + 5, _msg, _ml);
        yaafc_error_destroy(_r.error);
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

static size_t sharded_storage_db_get_skel(const void *_body, size_t _body_len,
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
    double span_start = yaafc_ytime_monotonic_sec();
    struct yaafc_string_result _r = sharded_storage_db_get(&_local, _obj, _hdrs, _s1, _s2);
    {
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        const char *span_trace = _hdrs ? yheaders_get(_hdrs, "trace_id") : "-";
        ydebug("span trace=%s op=skel.sharded_storage_db_get dur_us=%.0f", span_trace ? span_trace : "-", span_us);
        yspan_record("skel.sharded_storage_db_get", span_us);
    }
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {
        yaafc_error_print(stderr, "[skel] sharded_storage_db_get", _r.error);
        const char *_msg = _r.error.msg ? _r.error.msg : "(no msg)";
        uint32_t _ml = (uint32_t)strlen(_msg);
        if (_ml > 256) _ml = 256;
        if (_resp_max < 1 + 4 + _ml) {
            yaafc_error_destroy(_r.error);
            ((uint8_t *)_resp)[0] = 1;
            return _resp_max >= 1 ? 1 : 0;
        }
        ((uint8_t *)_resp)[0] = 1;
        memcpy((uint8_t *)_resp + 1, &_ml, 4);
        memcpy((uint8_t *)_resp + 5, _msg, _ml);
        yaafc_error_destroy(_r.error);
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

static size_t sharded_storage_db_exists_skel(const void *_body, size_t _body_len,
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
    double span_start = yaafc_ytime_monotonic_sec();
    struct yaafc_int_result _r = sharded_storage_db_exists(&_local, _obj, _hdrs, _s1, _s2);
    {
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        const char *span_trace = _hdrs ? yheaders_get(_hdrs, "trace_id") : "-";
        ydebug("span trace=%s op=skel.sharded_storage_db_exists dur_us=%.0f", span_trace ? span_trace : "-", span_us);
        yspan_record("skel.sharded_storage_db_exists", span_us);
    }
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {
        yaafc_error_print(stderr, "[skel] sharded_storage_db_exists", _r.error);
        const char *_msg = _r.error.msg ? _r.error.msg : "(no msg)";
        uint32_t _ml = (uint32_t)strlen(_msg);
        if (_ml > 256) _ml = 256;
        if (_resp_max < 1 + 4 + _ml) {
            yaafc_error_destroy(_r.error);
            ((uint8_t *)_resp)[0] = 1;
            return _resp_max >= 1 ? 1 : 0;
        }
        ((uint8_t *)_resp)[0] = 1;
        memcpy((uint8_t *)_resp + 1, &_ml, 4);
        memcpy((uint8_t *)_resp + 5, _msg, _ml);
        yaafc_error_destroy(_r.error);
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

static size_t sharded_storage_db_del_skel(const void *_body, size_t _body_len,
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
    double span_start = yaafc_ytime_monotonic_sec();
    struct yaafc_int_result _r = sharded_storage_db_del(&_local, _obj, _hdrs, _s1, _s2);
    {
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        const char *span_trace = _hdrs ? yheaders_get(_hdrs, "trace_id") : "-";
        ydebug("span trace=%s op=skel.sharded_storage_db_del dur_us=%.0f", span_trace ? span_trace : "-", span_us);
        yspan_record("skel.sharded_storage_db_del", span_us);
    }
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {
        yaafc_error_print(stderr, "[skel] sharded_storage_db_del", _r.error);
        const char *_msg = _r.error.msg ? _r.error.msg : "(no msg)";
        uint32_t _ml = (uint32_t)strlen(_msg);
        if (_ml > 256) _ml = 256;
        if (_resp_max < 1 + 4 + _ml) {
            yaafc_error_destroy(_r.error);
            ((uint8_t *)_resp)[0] = 1;
            return _resp_max >= 1 ? 1 : 0;
        }
        ((uint8_t *)_resp)[0] = 1;
        memcpy((uint8_t *)_resp + 1, &_ml, 4);
        memcpy((uint8_t *)_resp + 5, _msg, _ml);
        yaafc_error_destroy(_r.error);
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

static size_t sharded_storage_db_count_skel(const void *_body, size_t _body_len,
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
    double span_start = yaafc_ytime_monotonic_sec();
    struct yaafc_size_result _r = sharded_storage_db_count(&_local, _obj, _hdrs, _s1);
    {
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        const char *span_trace = _hdrs ? yheaders_get(_hdrs, "trace_id") : "-";
        ydebug("span trace=%s op=skel.sharded_storage_db_count dur_us=%.0f", span_trace ? span_trace : "-", span_us);
        yspan_record("skel.sharded_storage_db_count", span_us);
    }
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {
        yaafc_error_print(stderr, "[skel] sharded_storage_db_count", _r.error);
        const char *_msg = _r.error.msg ? _r.error.msg : "(no msg)";
        uint32_t _ml = (uint32_t)strlen(_msg);
        if (_ml > 256) _ml = 256;
        if (_resp_max < 1 + 4 + _ml) {
            yaafc_error_destroy(_r.error);
            ((uint8_t *)_resp)[0] = 1;
            return _resp_max >= 1 ? 1 : 0;
        }
        ((uint8_t *)_resp)[0] = 1;
        memcpy((uint8_t *)_resp + 1, &_ml, 4);
        memcpy((uint8_t *)_resp + 5, _msg, _ml);
        yaafc_error_destroy(_r.error);
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

static int sharded_storage_db_set_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    const char *arg0 = yjson_as_string(yjson_array_at(args, 0), "");
    const char *arg1 = yjson_as_string(yjson_array_at(args, 1), "");
    const char *arg2 = yjson_as_string(yjson_array_at(args, 2), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct yaafc_int_result call_result = sharded_storage_db_set(call_ctx, obj, hdrs, arg0, arg1, arg2);
    if (YAAFC_IS_ERR(call_result)) {
        snprintf(err, err_cap, "%s: %s", "sharded_storage_db_set",
                 call_result.error.msg ? call_result.error.msg : "<no message>");
        yaafc_error_destroy(call_result.error);
        return -1;
    }
    yjson_w_int(result, (int64_t)call_result.value);
    return 0;
}

static int sharded_storage_db_get_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    const char *arg0 = yjson_as_string(yjson_array_at(args, 0), "");
    const char *arg1 = yjson_as_string(yjson_array_at(args, 1), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct yaafc_string_result call_result = sharded_storage_db_get(call_ctx, obj, hdrs, arg0, arg1);
    if (YAAFC_IS_ERR(call_result)) {
        snprintf(err, err_cap, "%s: %s", "sharded_storage_db_get",
                 call_result.error.msg ? call_result.error.msg : "<no message>");
        yaafc_error_destroy(call_result.error);
        return -1;
    }
    yjson_w_string(result, call_result.value ? call_result.value : "");
    free(call_result.value);
    return 0;
}

static int sharded_storage_db_exists_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    const char *arg0 = yjson_as_string(yjson_array_at(args, 0), "");
    const char *arg1 = yjson_as_string(yjson_array_at(args, 1), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct yaafc_int_result call_result = sharded_storage_db_exists(call_ctx, obj, hdrs, arg0, arg1);
    if (YAAFC_IS_ERR(call_result)) {
        snprintf(err, err_cap, "%s: %s", "sharded_storage_db_exists",
                 call_result.error.msg ? call_result.error.msg : "<no message>");
        yaafc_error_destroy(call_result.error);
        return -1;
    }
    yjson_w_int(result, (int64_t)call_result.value);
    return 0;
}

static int sharded_storage_db_del_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    const char *arg0 = yjson_as_string(yjson_array_at(args, 0), "");
    const char *arg1 = yjson_as_string(yjson_array_at(args, 1), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct yaafc_int_result call_result = sharded_storage_db_del(call_ctx, obj, hdrs, arg0, arg1);
    if (YAAFC_IS_ERR(call_result)) {
        snprintf(err, err_cap, "%s: %s", "sharded_storage_db_del",
                 call_result.error.msg ? call_result.error.msg : "<no message>");
        yaafc_error_destroy(call_result.error);
        return -1;
    }
    yjson_w_int(result, (int64_t)call_result.value);
    return 0;
}

static int sharded_storage_db_count_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    const char *arg0 = yjson_as_string(yjson_array_at(args, 0), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct yaafc_size_result call_result = sharded_storage_db_count(call_ctx, obj, hdrs, arg0);
    if (YAAFC_IS_ERR(call_result)) {
        snprintf(err, err_cap, "%s: %s", "sharded_storage_db_count",
                 call_result.error.msg ? call_result.error.msg : "<no message>");
        yaafc_error_destroy(call_result.error);
        return -1;
    }
    yjson_w_int(result, (int64_t)call_result.value);
    return 0;
}

struct object_ptr_result sharded_storage_db_create(struct ctx *ctx)
{
    ydebug("class=sharded_storage_db");
    struct class_ptr_result _kr = sharded_storage_db_class_get();
    if (YAAFC_IS_ERR(_kr))
        return YAAFC_ERR(object_ptr, "sharded_storage_db_create: class accessor failed", _kr);
    /* A service dependency is acquired once and cached for the connection
     * (remote) / process (in-process) lifetime — no per-call create. */
    return rpc_object_acquire(ctx, _kr.value, "sharded_storage_db");
}


/* ---- sharded_storage: jinvoke table ------------------------------------ */

struct sharded_storage_jinvoke_row { const char *name; jinvoke_fn fn; };

static const struct sharded_storage_jinvoke_row sharded_storage_jinvoke_rows[] = {
    {"sharded_storage_db_set", sharded_storage_db_set_jinvoke},
    {"sharded_storage_db_get", sharded_storage_db_get_jinvoke},
    {"sharded_storage_db_exists", sharded_storage_db_exists_jinvoke},
    {"sharded_storage_db_del", sharded_storage_db_del_jinvoke},
    {"sharded_storage_db_count", sharded_storage_db_count_jinvoke}
};

static jinvoke_fn sharded_storage_jinvoke_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(sharded_storage_jinvoke_rows) / sizeof(sharded_storage_jinvoke_rows[0]); ++i)
        if (strcmp(sharded_storage_jinvoke_rows[i].name, qname) == 0)
            return sharded_storage_jinvoke_rows[i].fn;
    return NULL;
}
/* ---- sharded_storage: class name → accessor (lazy) ---------------------- */

static struct class_ptr_result sharded_storage_accessor_lookup(const char *name)
{
    if (strcmp(name, "sharded_storage_db") == 0) return sharded_storage_db_class_get();
    return YAAFC_OK(class_ptr, NULL);
}

/* ---- sharded_storage: slot → skel, name-keyed static data --------------- */

struct sharded_storage_skel_row { const char *name; rpc_skel_fn fn; };

static const struct sharded_storage_skel_row sharded_storage_skel_rows[] = {
    {"sharded_storage_db_set", sharded_storage_db_set_skel},
    {"sharded_storage_db_get", sharded_storage_db_get_skel},
    {"sharded_storage_db_exists", sharded_storage_db_exists_skel},
    {"sharded_storage_db_del", sharded_storage_db_del_skel},
    {"sharded_storage_db_count", sharded_storage_db_count_skel}
};

static rpc_skel_fn sharded_storage_skel_lookup(method_slot slot)
{
    struct const_char_ptr_result nr = method_slot_name(slot);
    if (YAAFC_IS_ERR(nr)) { yaafc_error_destroy(nr.error); return NULL; }
    const char *name = nr.value;
    for (size_t i = 0; i < sizeof(sharded_storage_skel_rows) / sizeof(sharded_storage_skel_rows[0]); ++i)
        if (strcmp(sharded_storage_skel_rows[i].name, name) == 0)
            return sharded_storage_skel_rows[i].fn;
    return NULL;
}

/* ---- sharded_storage: install hooks before main ------------------------- */

__attribute__((constructor))
static void sharded_storage_install_hooks(void)
{
    struct yaafc_void_result _ar = class_add_accessor_lookup(sharded_storage_accessor_lookup);
    if (YAAFC_IS_ERR(_ar)) {
        yaafc_error_print(stderr, "sharded_storage_install_hooks", _ar.error);
        yaafc_error_destroy(_ar.error);
        abort();
    }
    rpc_add_skel_lookup(sharded_storage_skel_lookup);
    jinvoke_add_lookup(sharded_storage_jinvoke_lookup);
    { struct class_ptr_result reg = sharded_storage_db_class_get();
      if (YAAFC_IS_ERR(reg)) yaafc_error_destroy(reg.error); }
}
