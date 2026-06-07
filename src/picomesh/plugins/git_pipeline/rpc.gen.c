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
#include "git_pipeline.internal.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t git_pipeline_git_pipeline_enqueue_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_pipeline_git_pipeline_enqueue");
    struct picomesh_uint32_result _r = git_pipeline_git_pipeline_enqueue(&_local, _obj, _hdrs, _v1);
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

static size_t git_pipeline_git_pipeline_enqueue_job_skel(const void *_body, size_t _body_len,
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
    int64_t _v4 = 0;
    if (_off + sizeof(_v4) > _body_len) goto _short_body;
    memcpy(&_v4, (const uint8_t *)_body + _off, sizeof(_v4));
    _off += sizeof(_v4);
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_pipeline_git_pipeline_enqueue_job");
    struct picomesh_uint32_result _r = git_pipeline_git_pipeline_enqueue_job(&_local, _obj, _hdrs, _v1, _s2, _s3, _v4);
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

static size_t git_pipeline_git_pipeline_lease_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_pipeline_git_pipeline_lease");
    struct picomesh_uint32_result _r = git_pipeline_git_pipeline_lease(&_local, _obj, _hdrs, _v1);
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

static size_t git_pipeline_git_pipeline_lease_job_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_pipeline_git_pipeline_lease_job");
    struct picomesh_json_result _r = git_pipeline_git_pipeline_lease_job(&_local, _obj, _hdrs, _v1, _s2);
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

static size_t git_pipeline_git_pipeline_job_descriptor_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_pipeline_git_pipeline_job_descriptor");
    struct picomesh_json_result _r = git_pipeline_git_pipeline_job_descriptor(&_local, _obj, _hdrs, _v1);
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

static size_t git_pipeline_git_pipeline_append_log_skel(const void *_body, size_t _body_len,
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
    int64_t _v2 = 0;
    if (_off + sizeof(_v2) > _body_len) goto _short_body;
    memcpy(&_v2, (const uint8_t *)_body + _off, sizeof(_v2));
    _off += sizeof(_v2);
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_pipeline_git_pipeline_append_log");
    struct picomesh_int64_result _r = git_pipeline_git_pipeline_append_log(&_local, _obj, _hdrs, _v1, _v2, _s3);
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

static size_t git_pipeline_git_pipeline_read_log_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_pipeline_git_pipeline_read_log");
    struct picomesh_string_result _r = git_pipeline_git_pipeline_read_log(&_local, _obj, _hdrs, _v1);
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

static size_t git_pipeline_git_pipeline_complete_skel(const void *_body, size_t _body_len,
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
    int32_t _v2 = 0;
    if (_off + sizeof(_v2) > _body_len) goto _short_body;
    memcpy(&_v2, (const uint8_t *)_body + _off, sizeof(_v2));
    _off += sizeof(_v2);
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_pipeline_git_pipeline_complete");
    struct picomesh_int_result _r = git_pipeline_git_pipeline_complete(&_local, _obj, _hdrs, _v1, _v2);
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

static size_t git_pipeline_git_pipeline_complete_job_skel(const void *_body, size_t _body_len,
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
    int32_t _v2 = 0;
    if (_off + sizeof(_v2) > _body_len) goto _short_body;
    memcpy(&_v2, (const uint8_t *)_body + _off, sizeof(_v2));
    _off += sizeof(_v2);
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_pipeline_git_pipeline_complete_job");
    struct picomesh_int_result _r = git_pipeline_git_pipeline_complete_job(&_local, _obj, _hdrs, _v1, _v2, _s3);
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

static size_t git_pipeline_git_pipeline_requeue_expired_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_pipeline_git_pipeline_requeue_expired");
    struct picomesh_size_result _r = git_pipeline_git_pipeline_requeue_expired(&_local, _obj, _hdrs);
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

static size_t git_pipeline_git_pipeline_count_pending_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_pipeline_git_pipeline_count_pending");
    struct picomesh_size_result _r = git_pipeline_git_pipeline_count_pending(&_local, _obj, _hdrs);
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

static size_t git_pipeline_git_pipeline_count_running_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_pipeline_git_pipeline_count_running");
    struct picomesh_size_result _r = git_pipeline_git_pipeline_count_running(&_local, _obj, _hdrs);
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

static size_t git_pipeline_git_pipeline_count_done_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_pipeline_git_pipeline_count_done");
    struct picomesh_size_result _r = git_pipeline_git_pipeline_count_done(&_local, _obj, _hdrs);
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

static size_t git_pipeline_git_pipeline_list_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_pipeline_git_pipeline_list");
    struct picomesh_json_result _r = git_pipeline_git_pipeline_list(&_local, _obj, _hdrs, _v1, _v2);
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

static size_t git_pipeline_git_pipeline_list_all_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_pipeline_git_pipeline_list_all");
    struct picomesh_json_result _r = git_pipeline_git_pipeline_list_all(&_local, _obj, _hdrs);
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

static int git_pipeline_git_pipeline_enqueue_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_pipeline_git_pipeline_enqueue");
    uint32_t arg0 = (uint32_t)yjson_as_int(yjson_array_at(args, 0), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_uint32_result call_result = git_pipeline_git_pipeline_enqueue(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_pipeline_git_pipeline_enqueue",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_enqueue_job_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_pipeline_git_pipeline_enqueue_job");
    uint32_t arg0 = (uint32_t)yjson_as_int(yjson_array_at(args, 0), 0);
    const char *arg1 = yjson_as_string(yjson_array_at(args, 1), "");
    const char *arg2 = yjson_as_string(yjson_array_at(args, 2), "");
    int64_t arg3 = (int64_t)yjson_as_int(yjson_array_at(args, 3), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_uint32_result call_result = git_pipeline_git_pipeline_enqueue_job(call_ctx, obj, hdrs, arg0, arg1, arg2, arg3);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_pipeline_git_pipeline_enqueue_job",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_lease_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_pipeline_git_pipeline_lease");
    uint32_t arg0 = (uint32_t)yjson_as_int(yjson_array_at(args, 0), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_uint32_result call_result = git_pipeline_git_pipeline_lease(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_pipeline_git_pipeline_lease",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_lease_job_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_pipeline_git_pipeline_lease_job");
    uint32_t arg0 = (uint32_t)yjson_as_int(yjson_array_at(args, 0), 0);
    const char *arg1 = yjson_as_string(yjson_array_at(args, 1), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = git_pipeline_git_pipeline_lease_job(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_pipeline_git_pipeline_lease_job",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_raw(result, call_result.value ? call_result.value : "null");
    free(call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_job_descriptor_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_pipeline_git_pipeline_job_descriptor");
    uint32_t arg0 = (uint32_t)yjson_as_int(yjson_array_at(args, 0), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = git_pipeline_git_pipeline_job_descriptor(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_pipeline_git_pipeline_job_descriptor",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_raw(result, call_result.value ? call_result.value : "null");
    free(call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_append_log_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_pipeline_git_pipeline_append_log");
    uint32_t arg0 = (uint32_t)yjson_as_int(yjson_array_at(args, 0), 0);
    int64_t arg1 = (int64_t)yjson_as_int(yjson_array_at(args, 1), 0);
    const char *arg2 = yjson_as_string(yjson_array_at(args, 2), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = git_pipeline_git_pipeline_append_log(call_ctx, obj, hdrs, arg0, arg1, arg2);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_pipeline_git_pipeline_append_log",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_read_log_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_pipeline_git_pipeline_read_log");
    uint32_t arg0 = (uint32_t)yjson_as_int(yjson_array_at(args, 0), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_string_result call_result = git_pipeline_git_pipeline_read_log(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_pipeline_git_pipeline_read_log",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_string(result, call_result.value ? call_result.value : "");
    free(call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_complete_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_pipeline_git_pipeline_complete");
    uint32_t arg0 = (uint32_t)yjson_as_int(yjson_array_at(args, 0), 0);
    int32_t arg1 = (int32_t)yjson_as_int(yjson_array_at(args, 1), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = git_pipeline_git_pipeline_complete(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_pipeline_git_pipeline_complete",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_complete_job_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_pipeline_git_pipeline_complete_job");
    uint32_t arg0 = (uint32_t)yjson_as_int(yjson_array_at(args, 0), 0);
    int32_t arg1 = (int32_t)yjson_as_int(yjson_array_at(args, 1), 0);
    const char *arg2 = yjson_as_string(yjson_array_at(args, 2), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = git_pipeline_git_pipeline_complete_job(call_ctx, obj, hdrs, arg0, arg1, arg2);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_pipeline_git_pipeline_complete_job",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_requeue_expired_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_pipeline_git_pipeline_requeue_expired");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = git_pipeline_git_pipeline_requeue_expired(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_pipeline_git_pipeline_requeue_expired",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_count_pending_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_pipeline_git_pipeline_count_pending");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = git_pipeline_git_pipeline_count_pending(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_pipeline_git_pipeline_count_pending",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_count_running_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_pipeline_git_pipeline_count_running");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = git_pipeline_git_pipeline_count_running(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_pipeline_git_pipeline_count_running",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_count_done_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_pipeline_git_pipeline_count_done");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = git_pipeline_git_pipeline_count_done(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_pipeline_git_pipeline_count_done",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_list_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_pipeline_git_pipeline_list");
    int64_t arg0 = (int64_t)yjson_as_int(yjson_array_at(args, 0), 0);
    int64_t arg1 = (int64_t)yjson_as_int(yjson_array_at(args, 1), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = git_pipeline_git_pipeline_list(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_pipeline_git_pipeline_list",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_raw(result, call_result.value ? call_result.value : "null");
    free(call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_list_all_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_pipeline_git_pipeline_list_all");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = git_pipeline_git_pipeline_list_all(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_pipeline_git_pipeline_list_all",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_raw(result, call_result.value ? call_result.value : "null");
    free(call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_enqueue_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "git_pipeline_git_pipeline_enqueue: expected 1 arg(s), got %u", _argc);
        return -1;
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "repo_id: expected unsigned int (%s)", cmp_strerror(_mr)); return -1; }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "repo_id: value %llu out of range for uint32_t", (unsigned long long)_u); return -1; }
        _v0 = (uint32_t)_u;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_uint32_result call_result = git_pipeline_git_pipeline_enqueue(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_pipeline_git_pipeline_enqueue",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_uinteger(_mw, (uint64_t)call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_enqueue_job_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 4u) {
        snprintf(_err, _err_cap, "git_pipeline_git_pipeline_enqueue_job: expected 4 arg(s), got %u", _argc);
        return -1;
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "repo_id: expected unsigned int (%s)", cmp_strerror(_mr)); return -1; }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "repo_id: value %llu out of range for uint32_t", (unsigned long long)_u); return -1; }
        _v0 = (uint32_t)_u;
    }
    char _v1[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v1);
        if (!cmp_read_str(_mr, _v1, &_sz)) {
            snprintf(_err, _err_cap, "ref: expected str arg (%s)", cmp_strerror(_mr));
            return -1;
        }
    }
    char _v2[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v2);
        if (!cmp_read_str(_mr, _v2, &_sz)) {
            snprintf(_err, _err_cap, "pipeline_path: expected str arg (%s)", cmp_strerror(_mr));
            return -1;
        }
    }
    int64_t _v3;
    if (!cmp_read_integer(_mr, &_v3)) { snprintf(_err, _err_cap, "timeout_seconds: expected int (%s)", cmp_strerror(_mr)); return -1; }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_uint32_result call_result = git_pipeline_git_pipeline_enqueue_job(call_ctx, obj, hdrs, _v0, _v1, _v2, _v3);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_pipeline_git_pipeline_enqueue_job",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_uinteger(_mw, (uint64_t)call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_lease_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "git_pipeline_git_pipeline_lease: expected 1 arg(s), got %u", _argc);
        return -1;
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "runner_id: expected unsigned int (%s)", cmp_strerror(_mr)); return -1; }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "runner_id: value %llu out of range for uint32_t", (unsigned long long)_u); return -1; }
        _v0 = (uint32_t)_u;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_uint32_result call_result = git_pipeline_git_pipeline_lease(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_pipeline_git_pipeline_lease",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_uinteger(_mw, (uint64_t)call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_lease_job_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "git_pipeline_git_pipeline_lease_job: expected 2 arg(s), got %u", _argc);
        return -1;
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "runner_id: expected unsigned int (%s)", cmp_strerror(_mr)); return -1; }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "runner_id: value %llu out of range for uint32_t", (unsigned long long)_u); return -1; }
        _v0 = (uint32_t)_u;
    }
    char _v1[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v1);
        if (!cmp_read_str(_mr, _v1, &_sz)) {
            snprintf(_err, _err_cap, "labels: expected str arg (%s)", cmp_strerror(_mr));
            return -1;
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = git_pipeline_git_pipeline_lease_job(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_pipeline_git_pipeline_lease_job",
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

static int git_pipeline_git_pipeline_job_descriptor_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "git_pipeline_git_pipeline_job_descriptor: expected 1 arg(s), got %u", _argc);
        return -1;
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "job_id: expected unsigned int (%s)", cmp_strerror(_mr)); return -1; }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "job_id: value %llu out of range for uint32_t", (unsigned long long)_u); return -1; }
        _v0 = (uint32_t)_u;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = git_pipeline_git_pipeline_job_descriptor(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_pipeline_git_pipeline_job_descriptor",
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

static int git_pipeline_git_pipeline_append_log_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 3u) {
        snprintf(_err, _err_cap, "git_pipeline_git_pipeline_append_log: expected 3 arg(s), got %u", _argc);
        return -1;
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "job_id: expected unsigned int (%s)", cmp_strerror(_mr)); return -1; }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "job_id: value %llu out of range for uint32_t", (unsigned long long)_u); return -1; }
        _v0 = (uint32_t)_u;
    }
    int64_t _v1;
    if (!cmp_read_integer(_mr, &_v1)) { snprintf(_err, _err_cap, "offset: expected int (%s)", cmp_strerror(_mr)); return -1; }
    char _v2[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v2);
        if (!cmp_read_str(_mr, _v2, &_sz)) {
            snprintf(_err, _err_cap, "chunk: expected str arg (%s)", cmp_strerror(_mr));
            return -1;
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = git_pipeline_git_pipeline_append_log(call_ctx, obj, hdrs, _v0, _v1, _v2);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_pipeline_git_pipeline_append_log",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_read_log_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "git_pipeline_git_pipeline_read_log: expected 1 arg(s), got %u", _argc);
        return -1;
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "job_id: expected unsigned int (%s)", cmp_strerror(_mr)); return -1; }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "job_id: value %llu out of range for uint32_t", (unsigned long long)_u); return -1; }
        _v0 = (uint32_t)_u;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_string_result call_result = git_pipeline_git_pipeline_read_log(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_pipeline_git_pipeline_read_log",
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

static int git_pipeline_git_pipeline_complete_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "git_pipeline_git_pipeline_complete: expected 2 arg(s), got %u", _argc);
        return -1;
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "job_id: expected unsigned int (%s)", cmp_strerror(_mr)); return -1; }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "job_id: value %llu out of range for uint32_t", (unsigned long long)_u); return -1; }
        _v0 = (uint32_t)_u;
    }
    int32_t _v1;
    {
        int64_t _i;
        if (!cmp_read_integer(_mr, &_i)) { snprintf(_err, _err_cap, "status: expected int (%s)", cmp_strerror(_mr)); return -1; }
        if (_i < (INT32_MIN) || _i > (INT32_MAX)) { snprintf(_err, _err_cap, "status: value %lld out of range for int32_t", (long long)_i); return -1; }
        _v1 = (int32_t)_i;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = git_pipeline_git_pipeline_complete(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_pipeline_git_pipeline_complete",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_complete_job_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 3u) {
        snprintf(_err, _err_cap, "git_pipeline_git_pipeline_complete_job: expected 3 arg(s), got %u", _argc);
        return -1;
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "job_id: expected unsigned int (%s)", cmp_strerror(_mr)); return -1; }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "job_id: value %llu out of range for uint32_t", (unsigned long long)_u); return -1; }
        _v0 = (uint32_t)_u;
    }
    int32_t _v1;
    {
        int64_t _i;
        if (!cmp_read_integer(_mr, &_i)) { snprintf(_err, _err_cap, "status: expected int (%s)", cmp_strerror(_mr)); return -1; }
        if (_i < (INT32_MIN) || _i > (INT32_MAX)) { snprintf(_err, _err_cap, "status: value %lld out of range for int32_t", (long long)_i); return -1; }
        _v1 = (int32_t)_i;
    }
    char _v2[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v2);
        if (!cmp_read_str(_mr, _v2, &_sz)) {
            snprintf(_err, _err_cap, "summary: expected str arg (%s)", cmp_strerror(_mr));
            return -1;
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = git_pipeline_git_pipeline_complete_job(call_ctx, obj, hdrs, _v0, _v1, _v2);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_pipeline_git_pipeline_complete_job",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_requeue_expired_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 0u) {
        snprintf(_err, _err_cap, "git_pipeline_git_pipeline_requeue_expired: expected 0 arg(s), got %u", _argc);
        return -1;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = git_pipeline_git_pipeline_requeue_expired(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_pipeline_git_pipeline_requeue_expired",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_uinteger(_mw, (uint64_t)call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_count_pending_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 0u) {
        snprintf(_err, _err_cap, "git_pipeline_git_pipeline_count_pending: expected 0 arg(s), got %u", _argc);
        return -1;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = git_pipeline_git_pipeline_count_pending(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_pipeline_git_pipeline_count_pending",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_uinteger(_mw, (uint64_t)call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_count_running_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 0u) {
        snprintf(_err, _err_cap, "git_pipeline_git_pipeline_count_running: expected 0 arg(s), got %u", _argc);
        return -1;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = git_pipeline_git_pipeline_count_running(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_pipeline_git_pipeline_count_running",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_uinteger(_mw, (uint64_t)call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_count_done_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 0u) {
        snprintf(_err, _err_cap, "git_pipeline_git_pipeline_count_done: expected 0 arg(s), got %u", _argc);
        return -1;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = git_pipeline_git_pipeline_count_done(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_pipeline_git_pipeline_count_done",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_uinteger(_mw, (uint64_t)call_result.value);
    return 0;
}

static int git_pipeline_git_pipeline_list_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "git_pipeline_git_pipeline_list: expected 2 arg(s), got %u", _argc);
        return -1;
    }
    int64_t _v0;
    if (!cmp_read_integer(_mr, &_v0)) { snprintf(_err, _err_cap, "offset: expected int (%s)", cmp_strerror(_mr)); return -1; }
    int64_t _v1;
    if (!cmp_read_integer(_mr, &_v1)) { snprintf(_err, _err_cap, "limit: expected int (%s)", cmp_strerror(_mr)); return -1; }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = git_pipeline_git_pipeline_list(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_pipeline_git_pipeline_list",
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

static int git_pipeline_git_pipeline_list_all_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 0u) {
        snprintf(_err, _err_cap, "git_pipeline_git_pipeline_list_all: expected 0 arg(s), got %u", _argc);
        return -1;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = git_pipeline_git_pipeline_list_all(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_pipeline_git_pipeline_list_all",
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

struct object_ptr_result git_pipeline_git_pipeline_create(struct ctx *ctx)
{
    ydebug("class=git_pipeline_git_pipeline");
    struct class_ptr_result _kr = git_pipeline_git_pipeline_class_get();
    if (PICOMESH_IS_ERR(_kr))
        return PICOMESH_ERR(object_ptr, "git_pipeline_git_pipeline_create: class accessor failed", _kr);
    /* A service dependency is acquired once and cached for the connection
     * (remote) / process (in-process) lifetime — no per-call create. */
    return rpc_object_acquire(ctx, _kr.value, "git_pipeline_git_pipeline");
}


/* ---- git_pipeline: jinvoke table ------------------------------------ */

struct git_pipeline_jinvoke_row { const char *name; jinvoke_fn fn; };

static const struct git_pipeline_jinvoke_row git_pipeline_jinvoke_rows[] = {
    {"git_pipeline_git_pipeline_enqueue", git_pipeline_git_pipeline_enqueue_jinvoke},
    {"git_pipeline_git_pipeline_enqueue_job", git_pipeline_git_pipeline_enqueue_job_jinvoke},
    {"git_pipeline_git_pipeline_lease", git_pipeline_git_pipeline_lease_jinvoke},
    {"git_pipeline_git_pipeline_lease_job", git_pipeline_git_pipeline_lease_job_jinvoke},
    {"git_pipeline_git_pipeline_job_descriptor", git_pipeline_git_pipeline_job_descriptor_jinvoke},
    {"git_pipeline_git_pipeline_append_log", git_pipeline_git_pipeline_append_log_jinvoke},
    {"git_pipeline_git_pipeline_read_log", git_pipeline_git_pipeline_read_log_jinvoke},
    {"git_pipeline_git_pipeline_complete", git_pipeline_git_pipeline_complete_jinvoke},
    {"git_pipeline_git_pipeline_complete_job", git_pipeline_git_pipeline_complete_job_jinvoke},
    {"git_pipeline_git_pipeline_requeue_expired", git_pipeline_git_pipeline_requeue_expired_jinvoke},
    {"git_pipeline_git_pipeline_count_pending", git_pipeline_git_pipeline_count_pending_jinvoke},
    {"git_pipeline_git_pipeline_count_running", git_pipeline_git_pipeline_count_running_jinvoke},
    {"git_pipeline_git_pipeline_count_done", git_pipeline_git_pipeline_count_done_jinvoke},
    {"git_pipeline_git_pipeline_list", git_pipeline_git_pipeline_list_jinvoke},
    {"git_pipeline_git_pipeline_list_all", git_pipeline_git_pipeline_list_all_jinvoke}
};

static jinvoke_fn git_pipeline_jinvoke_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(git_pipeline_jinvoke_rows) / sizeof(git_pipeline_jinvoke_rows[0]); ++i)
        if (strcmp(git_pipeline_jinvoke_rows[i].name, qname) == 0)
            return git_pipeline_jinvoke_rows[i].fn;
    return NULL;
}

/* ---- git_pipeline: minvoke table ------------------------------------ */

struct git_pipeline_minvoke_row { const char *name; minvoke_fn fn; };

static const struct git_pipeline_minvoke_row git_pipeline_minvoke_rows[] = {
    {"git_pipeline_git_pipeline_enqueue", git_pipeline_git_pipeline_enqueue_minvoke},
    {"git_pipeline_git_pipeline_enqueue_job", git_pipeline_git_pipeline_enqueue_job_minvoke},
    {"git_pipeline_git_pipeline_lease", git_pipeline_git_pipeline_lease_minvoke},
    {"git_pipeline_git_pipeline_lease_job", git_pipeline_git_pipeline_lease_job_minvoke},
    {"git_pipeline_git_pipeline_job_descriptor", git_pipeline_git_pipeline_job_descriptor_minvoke},
    {"git_pipeline_git_pipeline_append_log", git_pipeline_git_pipeline_append_log_minvoke},
    {"git_pipeline_git_pipeline_read_log", git_pipeline_git_pipeline_read_log_minvoke},
    {"git_pipeline_git_pipeline_complete", git_pipeline_git_pipeline_complete_minvoke},
    {"git_pipeline_git_pipeline_complete_job", git_pipeline_git_pipeline_complete_job_minvoke},
    {"git_pipeline_git_pipeline_requeue_expired", git_pipeline_git_pipeline_requeue_expired_minvoke},
    {"git_pipeline_git_pipeline_count_pending", git_pipeline_git_pipeline_count_pending_minvoke},
    {"git_pipeline_git_pipeline_count_running", git_pipeline_git_pipeline_count_running_minvoke},
    {"git_pipeline_git_pipeline_count_done", git_pipeline_git_pipeline_count_done_minvoke},
    {"git_pipeline_git_pipeline_list", git_pipeline_git_pipeline_list_minvoke},
    {"git_pipeline_git_pipeline_list_all", git_pipeline_git_pipeline_list_all_minvoke}
};

static minvoke_fn git_pipeline_minvoke_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(git_pipeline_minvoke_rows) / sizeof(git_pipeline_minvoke_rows[0]); ++i)
        if (strcmp(git_pipeline_minvoke_rows[i].name, qname) == 0)
            return git_pipeline_minvoke_rows[i].fn;
    return NULL;
}

/* ---- git_pipeline: per-method parameter signatures (runtime reflection) -- */

static const struct jinvoke_param git_pipeline_git_pipeline_enqueue_params[] = {
    {"repo_id", "uint32_t"}
};
static const struct jinvoke_param git_pipeline_git_pipeline_enqueue_job_params[] = {
    {"repo_id", "uint32_t"},
    {"ref", "const char *"},
    {"pipeline_path", "const char *"},
    {"timeout_seconds", "int64_t"}
};
static const struct jinvoke_param git_pipeline_git_pipeline_lease_params[] = {
    {"runner_id", "uint32_t"}
};
static const struct jinvoke_param git_pipeline_git_pipeline_lease_job_params[] = {
    {"runner_id", "uint32_t"},
    {"labels", "const char *"}
};
static const struct jinvoke_param git_pipeline_git_pipeline_job_descriptor_params[] = {
    {"job_id", "uint32_t"}
};
static const struct jinvoke_param git_pipeline_git_pipeline_append_log_params[] = {
    {"job_id", "uint32_t"},
    {"offset", "int64_t"},
    {"chunk", "const char *"}
};
static const struct jinvoke_param git_pipeline_git_pipeline_read_log_params[] = {
    {"job_id", "uint32_t"}
};
static const struct jinvoke_param git_pipeline_git_pipeline_complete_params[] = {
    {"job_id", "uint32_t"},
    {"status", "int32_t"}
};
static const struct jinvoke_param git_pipeline_git_pipeline_complete_job_params[] = {
    {"job_id", "uint32_t"},
    {"status", "int32_t"},
    {"summary", "const char *"}
};
static const struct jinvoke_param git_pipeline_git_pipeline_list_params[] = {
    {"offset", "int64_t"},
    {"limit", "int64_t"}
};
struct git_pipeline_params_row { const char *name; struct jinvoke_params params; };

static const struct git_pipeline_params_row git_pipeline_params_rows[] = {
    {"git_pipeline_git_pipeline_enqueue", {git_pipeline_git_pipeline_enqueue_params, 1}},
    {"git_pipeline_git_pipeline_enqueue_job", {git_pipeline_git_pipeline_enqueue_job_params, 4}},
    {"git_pipeline_git_pipeline_lease", {git_pipeline_git_pipeline_lease_params, 1}},
    {"git_pipeline_git_pipeline_lease_job", {git_pipeline_git_pipeline_lease_job_params, 2}},
    {"git_pipeline_git_pipeline_job_descriptor", {git_pipeline_git_pipeline_job_descriptor_params, 1}},
    {"git_pipeline_git_pipeline_append_log", {git_pipeline_git_pipeline_append_log_params, 3}},
    {"git_pipeline_git_pipeline_read_log", {git_pipeline_git_pipeline_read_log_params, 1}},
    {"git_pipeline_git_pipeline_complete", {git_pipeline_git_pipeline_complete_params, 2}},
    {"git_pipeline_git_pipeline_complete_job", {git_pipeline_git_pipeline_complete_job_params, 3}},
    {"git_pipeline_git_pipeline_requeue_expired", {NULL, 0}},
    {"git_pipeline_git_pipeline_count_pending", {NULL, 0}},
    {"git_pipeline_git_pipeline_count_running", {NULL, 0}},
    {"git_pipeline_git_pipeline_count_done", {NULL, 0}},
    {"git_pipeline_git_pipeline_list", {git_pipeline_git_pipeline_list_params, 2}},
    {"git_pipeline_git_pipeline_list_all", {NULL, 0}}
};

static const struct jinvoke_params *git_pipeline_params_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(git_pipeline_params_rows) / sizeof(git_pipeline_params_rows[0]); ++i)
        if (strcmp(git_pipeline_params_rows[i].name, qname) == 0)
            return &git_pipeline_params_rows[i].params;
    return NULL;
}
/* ---- git_pipeline: class name → accessor (lazy) ---------------------- */

static struct class_ptr_result git_pipeline_accessor_lookup(const char *name)
{
    if (strcmp(name, "git_pipeline_git_pipeline") == 0) return git_pipeline_git_pipeline_class_get();
    return PICOMESH_OK(class_ptr, NULL);
}

/* ---- git_pipeline: slot → skel, name-keyed static data --------------- */

struct git_pipeline_skel_row { const char *name; rpc_skel_fn fn; };

static const struct git_pipeline_skel_row git_pipeline_skel_rows[] = {
    {"git_pipeline_git_pipeline_enqueue", git_pipeline_git_pipeline_enqueue_skel},
    {"git_pipeline_git_pipeline_enqueue_job", git_pipeline_git_pipeline_enqueue_job_skel},
    {"git_pipeline_git_pipeline_lease", git_pipeline_git_pipeline_lease_skel},
    {"git_pipeline_git_pipeline_lease_job", git_pipeline_git_pipeline_lease_job_skel},
    {"git_pipeline_git_pipeline_job_descriptor", git_pipeline_git_pipeline_job_descriptor_skel},
    {"git_pipeline_git_pipeline_append_log", git_pipeline_git_pipeline_append_log_skel},
    {"git_pipeline_git_pipeline_read_log", git_pipeline_git_pipeline_read_log_skel},
    {"git_pipeline_git_pipeline_complete", git_pipeline_git_pipeline_complete_skel},
    {"git_pipeline_git_pipeline_complete_job", git_pipeline_git_pipeline_complete_job_skel},
    {"git_pipeline_git_pipeline_requeue_expired", git_pipeline_git_pipeline_requeue_expired_skel},
    {"git_pipeline_git_pipeline_count_pending", git_pipeline_git_pipeline_count_pending_skel},
    {"git_pipeline_git_pipeline_count_running", git_pipeline_git_pipeline_count_running_skel},
    {"git_pipeline_git_pipeline_count_done", git_pipeline_git_pipeline_count_done_skel},
    {"git_pipeline_git_pipeline_list", git_pipeline_git_pipeline_list_skel},
    {"git_pipeline_git_pipeline_list_all", git_pipeline_git_pipeline_list_all_skel}
};

static rpc_skel_fn git_pipeline_skel_lookup(method_slot slot)
{
    struct const_char_ptr_result nr = method_slot_name(slot);
    if (PICOMESH_IS_ERR(nr)) { picomesh_error_destroy(nr.error); return NULL; }
    const char *name = nr.value;
    for (size_t i = 0; i < sizeof(git_pipeline_skel_rows) / sizeof(git_pipeline_skel_rows[0]); ++i)
        if (strcmp(git_pipeline_skel_rows[i].name, name) == 0)
            return git_pipeline_skel_rows[i].fn;
    return NULL;
}

/* ---- git_pipeline: registration entry point (called from the driver for
 *      config-ACTIVATED plugins only — registration is activation) ---- */

void picomesh_plugin_git_pipeline_register(void)
{
    struct picomesh_void_result _ar = class_add_accessor_lookup(git_pipeline_accessor_lookup);
    if (PICOMESH_IS_ERR(_ar)) {
        picomesh_error_print(stderr, "picomesh_plugin_git_pipeline_register", _ar.error);
        picomesh_error_destroy(_ar.error);
        abort();
    }
    rpc_add_skel_lookup(git_pipeline_skel_lookup);
    jinvoke_add_lookup(git_pipeline_jinvoke_lookup);
    minvoke_add_lookup(git_pipeline_minvoke_lookup);
    jinvoke_params_add_lookup(git_pipeline_params_lookup);
    { struct class_ptr_result reg = git_pipeline_git_pipeline_class_get();
      if (PICOMESH_IS_ERR(reg)) picomesh_error_destroy(reg.error); }
}
