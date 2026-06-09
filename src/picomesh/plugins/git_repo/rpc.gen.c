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
#include "git_repo.internal.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct picomesh_size_result git_repo_git_repo_make_skel(const void *_body, size_t _body_len,
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
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_repo_git_repo_make");
    struct picomesh_uint32_result _r = git_repo_git_repo_make(&_local, _obj, _hdrs, _v1, _s2, _s3);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_make_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_make_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result git_repo_git_repo_delete_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_repo_git_repo_delete");
    struct picomesh_int_result _r = git_repo_git_repo_delete(&_local, _obj, _hdrs, _v1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_delete_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_delete_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result git_repo_git_repo_owner_of_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_repo_git_repo_owner_of");
    struct picomesh_uint32_result _r = git_repo_git_repo_owner_of(&_local, _obj, _hdrs, _v1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_owner_of_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_owner_of_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result git_repo_git_repo_namespace_of_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_repo_git_repo_namespace_of");
    struct picomesh_string_result _r = git_repo_git_repo_namespace_of(&_local, _obj, _hdrs, _v1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_namespace_of_skel: response buffer too small");
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
        if (_resp_max < 1 + 4 + (size_t)_svlen) { free(_r.value); return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_namespace_of_skel: response buffer too small"); }
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

static struct picomesh_size_result git_repo_git_repo_count_for_owner_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_repo_git_repo_count_for_owner");
    struct picomesh_size_result _r = git_repo_git_repo_count_for_owner(&_local, _obj, _hdrs, _v1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_owner_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_owner_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result git_repo_git_repo_count_total_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_repo_git_repo_count_total");
    struct picomesh_size_result _r = git_repo_git_repo_count_total(&_local, _obj, _hdrs);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_total_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_total_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result git_repo_git_repo_list_for_owner_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_repo_git_repo_list_for_owner");
    struct picomesh_string_result _r = git_repo_git_repo_list_for_owner(&_local, _obj, _hdrs, _v1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_list_for_owner_skel: response buffer too small");
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
        if (_resp_max < 1 + 4 + (size_t)_svlen) { free(_r.value); return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_list_for_owner_skel: response buffer too small"); }
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

static struct picomesh_size_result git_repo_git_repo_list_for_namespace_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_repo_git_repo_list_for_namespace");
    struct picomesh_string_result _r = git_repo_git_repo_list_for_namespace(&_local, _obj, _hdrs, _s1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_list_for_namespace_skel: response buffer too small");
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
        if (_resp_max < 1 + 4 + (size_t)_svlen) { free(_r.value); return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_list_for_namespace_skel: response buffer too small"); }
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

static struct picomesh_size_result git_repo_git_repo_count_for_namespace_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_repo_git_repo_count_for_namespace");
    struct picomesh_size_result _r = git_repo_git_repo_count_for_namespace(&_local, _obj, _hdrs, _s1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_namespace_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_count_for_namespace_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result git_repo_git_repo_read_tree_skel(const void *_body, size_t _body_len,
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
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_repo_git_repo_read_tree");
    struct picomesh_string_result _r = git_repo_git_repo_read_tree(&_local, _obj, _hdrs, _v1, _s2, _s3);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_read_tree_skel: response buffer too small");
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
        if (_resp_max < 1 + 4 + (size_t)_svlen) { free(_r.value); return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_read_tree_skel: response buffer too small"); }
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

static struct picomesh_size_result git_repo_git_repo_read_file_skel(const void *_body, size_t _body_len,
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
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_repo_git_repo_read_file");
    struct picomesh_string_result _r = git_repo_git_repo_read_file(&_local, _obj, _hdrs, _v1, _s2, _s3);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_read_file_skel: response buffer too small");
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
        if (_resp_max < 1 + 4 + (size_t)_svlen) { free(_r.value); return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_read_file_skel: response buffer too small"); }
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

static struct picomesh_size_result git_repo_git_repo_put_file_skel(const void *_body, size_t _body_len,
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
    char _s4[4096];
    {
        if (_off + 4 > _body_len) goto _short_body;
        uint32_t _slen;
        memcpy(&_slen, (const uint8_t *)_body + _off, 4); _off += 4;
        if (_off + _slen > _body_len) goto _short_body;
        if (_slen >= sizeof(_s4)) goto _short_body;
        if (_slen) memcpy(_s4, (const uint8_t *)_body + _off, _slen);
        _s4[_slen] = 0; _off += _slen;
    }
    char _s5[4096];
    {
        if (_off + 4 > _body_len) goto _short_body;
        uint32_t _slen;
        memcpy(&_slen, (const uint8_t *)_body + _off, 4); _off += 4;
        if (_off + _slen > _body_len) goto _short_body;
        if (_slen >= sizeof(_s5)) goto _short_body;
        if (_slen) memcpy(_s5, (const uint8_t *)_body + _off, _slen);
        _s5[_slen] = 0; _off += _slen;
    }
    char _s6[4096];
    {
        if (_off + 4 > _body_len) goto _short_body;
        uint32_t _slen;
        memcpy(&_slen, (const uint8_t *)_body + _off, 4); _off += 4;
        if (_off + _slen > _body_len) goto _short_body;
        if (_slen >= sizeof(_s6)) goto _short_body;
        if (_slen) memcpy(_s6, (const uint8_t *)_body + _off, _slen);
        _s6[_slen] = 0; _off += _slen;
    }
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_repo_git_repo_put_file");
    struct picomesh_string_result _r = git_repo_git_repo_put_file(&_local, _obj, _hdrs, _v1, _s2, _s3, _s4, _s5, _s6);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_put_file_skel: response buffer too small");
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
        if (_resp_max < 1 + 4 + (size_t)_svlen) { free(_r.value); return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_put_file_skel: response buffer too small"); }
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

static struct picomesh_size_result git_repo_git_repo_is_public_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_repo_git_repo_is_public");
    struct picomesh_int_result _r = git_repo_git_repo_is_public(&_local, _obj, _hdrs, _v1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_is_public_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_is_public_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result git_repo_git_repo_set_public_skel(const void *_body, size_t _body_len,
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
    int _v2 = 0;
    if (_off + sizeof(_v2) > _body_len) goto _short_body;
    memcpy(&_v2, (const uint8_t *)_body + _off, sizeof(_v2));
    _off += sizeof(_v2);
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_repo_git_repo_set_public");
    struct picomesh_int_result _r = git_repo_git_repo_set_public(&_local, _obj, _hdrs, _v1, _v2);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_set_public_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_set_public_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result git_repo_git_repo_list_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_repo_git_repo_list");
    struct picomesh_json_result _r = git_repo_git_repo_list(&_local, _obj, _hdrs, _v1, _v2);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_list_skel: response buffer too small");
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
        if (_resp_max < 1 + 4 + (size_t)_svlen) { free(_r.value); return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_list_skel: response buffer too small"); }
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

static struct picomesh_size_result git_repo_git_repo_list_all_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.git_repo_git_repo_list_all");
    struct picomesh_json_result _r = git_repo_git_repo_list_all(&_local, _obj, _hdrs);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_list_all_skel: response buffer too small");
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
        if (_resp_max < 1 + 4 + (size_t)_svlen) { free(_r.value); return PICOMESH_ERR(picomesh_size, "git_repo_git_repo_list_all_skel: response buffer too small"); }
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

static struct picomesh_void_result git_repo_git_repo_make_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_repo_git_repo_make");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    const char *arg1 = json_as_string(json_array_at(args, 1), "");
    const char *arg2 = json_as_string(json_array_at(args, 2), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_uint32_result call_result = git_repo_git_repo_make(call_ctx, obj, hdrs, arg0, arg1, arg2);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_repo_git_repo_make",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_make", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_delete_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_repo_git_repo_delete");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = git_repo_git_repo_delete(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_repo_git_repo_delete",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_delete", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_owner_of_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_repo_git_repo_owner_of");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_uint32_result call_result = git_repo_git_repo_owner_of(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_repo_git_repo_owner_of",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_owner_of", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_namespace_of_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_repo_git_repo_namespace_of");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_string_result call_result = git_repo_git_repo_namespace_of(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_repo_git_repo_namespace_of",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_namespace_of", call_result);
    }
    json_writer_string(result, call_result.value ? call_result.value : "");
    free(call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_count_for_owner_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_repo_git_repo_count_for_owner");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = git_repo_git_repo_count_for_owner(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_repo_git_repo_count_for_owner",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_count_for_owner", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_count_total_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_repo_git_repo_count_total");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = git_repo_git_repo_count_total(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_repo_git_repo_count_total",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_count_total", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_list_for_owner_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_repo_git_repo_list_for_owner");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_string_result call_result = git_repo_git_repo_list_for_owner(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_repo_git_repo_list_for_owner",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_list_for_owner", call_result);
    }
    json_writer_string(result, call_result.value ? call_result.value : "");
    free(call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_list_for_namespace_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_repo_git_repo_list_for_namespace");
    const char *arg0 = json_as_string(json_array_at(args, 0), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_string_result call_result = git_repo_git_repo_list_for_namespace(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_repo_git_repo_list_for_namespace",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_list_for_namespace", call_result);
    }
    json_writer_string(result, call_result.value ? call_result.value : "");
    free(call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_count_for_namespace_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_repo_git_repo_count_for_namespace");
    const char *arg0 = json_as_string(json_array_at(args, 0), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = git_repo_git_repo_count_for_namespace(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_repo_git_repo_count_for_namespace",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_count_for_namespace", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_read_tree_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_repo_git_repo_read_tree");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    const char *arg1 = json_as_string(json_array_at(args, 1), "");
    const char *arg2 = json_as_string(json_array_at(args, 2), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_string_result call_result = git_repo_git_repo_read_tree(call_ctx, obj, hdrs, arg0, arg1, arg2);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_repo_git_repo_read_tree",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_read_tree", call_result);
    }
    json_writer_string(result, call_result.value ? call_result.value : "");
    free(call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_read_file_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_repo_git_repo_read_file");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    const char *arg1 = json_as_string(json_array_at(args, 1), "");
    const char *arg2 = json_as_string(json_array_at(args, 2), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_string_result call_result = git_repo_git_repo_read_file(call_ctx, obj, hdrs, arg0, arg1, arg2);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_repo_git_repo_read_file",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_read_file", call_result);
    }
    json_writer_string(result, call_result.value ? call_result.value : "");
    free(call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_put_file_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_repo_git_repo_put_file");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    const char *arg1 = json_as_string(json_array_at(args, 1), "");
    const char *arg2 = json_as_string(json_array_at(args, 2), "");
    const char *arg3 = json_as_string(json_array_at(args, 3), "");
    const char *arg4 = json_as_string(json_array_at(args, 4), "");
    const char *arg5 = json_as_string(json_array_at(args, 5), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_string_result call_result = git_repo_git_repo_put_file(call_ctx, obj, hdrs, arg0, arg1, arg2, arg3, arg4, arg5);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_repo_git_repo_put_file",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_put_file", call_result);
    }
    json_writer_string(result, call_result.value ? call_result.value : "");
    free(call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_is_public_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_repo_git_repo_is_public");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = git_repo_git_repo_is_public(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_repo_git_repo_is_public",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_is_public", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_set_public_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_repo_git_repo_set_public");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    int arg1 = (int)json_as_int(json_array_at(args, 1), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = git_repo_git_repo_set_public(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_repo_git_repo_set_public",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_set_public", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_list_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_repo_git_repo_list");
    int64_t arg0 = (int64_t)json_as_int(json_array_at(args, 0), 0);
    int64_t arg1 = (int64_t)json_as_int(json_array_at(args, 1), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = git_repo_git_repo_list(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_repo_git_repo_list",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_list", call_result);
    }
    json_writer_raw(result, call_result.value ? call_result.value : "null");
    free(call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_list_all_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] git_repo_git_repo_list_all");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = git_repo_git_repo_list_all(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "git_repo_git_repo_list_all",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_list_all", call_result);
    }
    json_writer_raw(result, call_result.value ? call_result.value : "null");
    free(call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_make_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 3u) {
        snprintf(_err, _err_cap, "git_repo_git_repo_make: expected 3 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_make: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "owner_id: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "owner_id: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    char _v1[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v1);
        if (!cmp_read_str(_mr, _v1, &_sz)) {
            snprintf(_err, _err_cap, "owner_name: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    char _v2[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v2);
        if (!cmp_read_str(_mr, _v2, &_sz)) {
            snprintf(_err, _err_cap, "repo_name: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_uint32_result call_result = git_repo_git_repo_make(call_ctx, obj, hdrs, _v0, _v1, _v2);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_repo_git_repo_make",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_make", call_result);
    }
    cmp_write_uinteger(_mw, (uint64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_delete_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "git_repo_git_repo_delete: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_delete: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "repo_id: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "repo_id: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = git_repo_git_repo_delete(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_repo_git_repo_delete",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_delete", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_owner_of_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "git_repo_git_repo_owner_of: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_owner_of: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "repo_id: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "repo_id: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_uint32_result call_result = git_repo_git_repo_owner_of(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_repo_git_repo_owner_of",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_owner_of", call_result);
    }
    cmp_write_uinteger(_mw, (uint64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_namespace_of_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "git_repo_git_repo_namespace_of: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_namespace_of: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "repo_id: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "repo_id: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_string_result call_result = git_repo_git_repo_namespace_of(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_repo_git_repo_namespace_of",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_namespace_of", call_result);
    }
    {
        const char *_sv = call_result.value ? call_result.value : "";
        cmp_write_str(_mw, _sv, (uint32_t)strlen(_sv));
        free(call_result.value);
    }
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_count_for_owner_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "git_repo_git_repo_count_for_owner: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_count_for_owner: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "owner_id: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "owner_id: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = git_repo_git_repo_count_for_owner(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_repo_git_repo_count_for_owner",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_count_for_owner", call_result);
    }
    cmp_write_uinteger(_mw, (uint64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_count_total_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 0u) {
        snprintf(_err, _err_cap, "git_repo_git_repo_count_total: expected 0 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_count_total: wrong argument count");
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = git_repo_git_repo_count_total(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_repo_git_repo_count_total",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_count_total", call_result);
    }
    cmp_write_uinteger(_mw, (uint64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_list_for_owner_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "git_repo_git_repo_list_for_owner: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_list_for_owner: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "owner_id: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "owner_id: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_string_result call_result = git_repo_git_repo_list_for_owner(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_repo_git_repo_list_for_owner",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_list_for_owner", call_result);
    }
    {
        const char *_sv = call_result.value ? call_result.value : "";
        cmp_write_str(_mw, _sv, (uint32_t)strlen(_sv));
        free(call_result.value);
    }
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_list_for_namespace_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "git_repo_git_repo_list_for_namespace: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_list_for_namespace: wrong argument count");
    }
    char _v0[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v0);
        if (!cmp_read_str(_mr, _v0, &_sz)) {
            snprintf(_err, _err_cap, "path: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_string_result call_result = git_repo_git_repo_list_for_namespace(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_repo_git_repo_list_for_namespace",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_list_for_namespace", call_result);
    }
    {
        const char *_sv = call_result.value ? call_result.value : "";
        cmp_write_str(_mw, _sv, (uint32_t)strlen(_sv));
        free(call_result.value);
    }
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_count_for_namespace_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "git_repo_git_repo_count_for_namespace: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_count_for_namespace: wrong argument count");
    }
    char _v0[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v0);
        if (!cmp_read_str(_mr, _v0, &_sz)) {
            snprintf(_err, _err_cap, "path: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = git_repo_git_repo_count_for_namespace(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_repo_git_repo_count_for_namespace",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_count_for_namespace", call_result);
    }
    cmp_write_uinteger(_mw, (uint64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_read_tree_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 3u) {
        snprintf(_err, _err_cap, "git_repo_git_repo_read_tree: expected 3 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_read_tree: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "repo_id: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "repo_id: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    char _v1[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v1);
        if (!cmp_read_str(_mr, _v1, &_sz)) {
            snprintf(_err, _err_cap, "ref: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    char _v2[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v2);
        if (!cmp_read_str(_mr, _v2, &_sz)) {
            snprintf(_err, _err_cap, "path: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_string_result call_result = git_repo_git_repo_read_tree(call_ctx, obj, hdrs, _v0, _v1, _v2);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_repo_git_repo_read_tree",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_read_tree", call_result);
    }
    {
        const char *_sv = call_result.value ? call_result.value : "";
        cmp_write_str(_mw, _sv, (uint32_t)strlen(_sv));
        free(call_result.value);
    }
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_read_file_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 3u) {
        snprintf(_err, _err_cap, "git_repo_git_repo_read_file: expected 3 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_read_file: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "repo_id: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "repo_id: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    char _v1[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v1);
        if (!cmp_read_str(_mr, _v1, &_sz)) {
            snprintf(_err, _err_cap, "ref: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    char _v2[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v2);
        if (!cmp_read_str(_mr, _v2, &_sz)) {
            snprintf(_err, _err_cap, "path: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_string_result call_result = git_repo_git_repo_read_file(call_ctx, obj, hdrs, _v0, _v1, _v2);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_repo_git_repo_read_file",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_read_file", call_result);
    }
    {
        const char *_sv = call_result.value ? call_result.value : "";
        cmp_write_str(_mw, _sv, (uint32_t)strlen(_sv));
        free(call_result.value);
    }
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_put_file_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 6u) {
        snprintf(_err, _err_cap, "git_repo_git_repo_put_file: expected 6 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_put_file: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "repo_id: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "repo_id: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    char _v1[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v1);
        if (!cmp_read_str(_mr, _v1, &_sz)) {
            snprintf(_err, _err_cap, "path: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    char _v2[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v2);
        if (!cmp_read_str(_mr, _v2, &_sz)) {
            snprintf(_err, _err_cap, "content: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    char _v3[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v3);
        if (!cmp_read_str(_mr, _v3, &_sz)) {
            snprintf(_err, _err_cap, "message: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    char _v4[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v4);
        if (!cmp_read_str(_mr, _v4, &_sz)) {
            snprintf(_err, _err_cap, "author_name: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    char _v5[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v5);
        if (!cmp_read_str(_mr, _v5, &_sz)) {
            snprintf(_err, _err_cap, "author_email: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_string_result call_result = git_repo_git_repo_put_file(call_ctx, obj, hdrs, _v0, _v1, _v2, _v3, _v4, _v5);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_repo_git_repo_put_file",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_put_file", call_result);
    }
    {
        const char *_sv = call_result.value ? call_result.value : "";
        cmp_write_str(_mw, _sv, (uint32_t)strlen(_sv));
        free(call_result.value);
    }
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_is_public_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "git_repo_git_repo_is_public: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_is_public: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "repo_id: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "repo_id: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = git_repo_git_repo_is_public(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_repo_git_repo_is_public",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_is_public", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_set_public_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "git_repo_git_repo_set_public: expected 2 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_set_public: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "repo_id: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "repo_id: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    int _v1;
    {
        int64_t _i;
        if (!cmp_read_integer(_mr, &_i)) { snprintf(_err, _err_cap, "is_public: expected int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_i < (INT_MIN) || _i > (INT_MAX)) { snprintf(_err, _err_cap, "is_public: value %lld out of range for int", (long long)_i); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v1 = (int)_i;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = git_repo_git_repo_set_public(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_repo_git_repo_set_public",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_set_public", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_list_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "git_repo_git_repo_list: expected 2 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_list: wrong argument count");
    }
    int64_t _v0;
    if (!cmp_read_integer(_mr, &_v0)) { snprintf(_err, _err_cap, "offset: expected int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
    int64_t _v1;
    if (!cmp_read_integer(_mr, &_v1)) { snprintf(_err, _err_cap, "limit: expected int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = git_repo_git_repo_list(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_repo_git_repo_list",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_list", call_result);
    }
    {
        const char *_sv = call_result.value ? call_result.value : "";
        cmp_write_str(_mw, _sv, (uint32_t)strlen(_sv));
        free(call_result.value);
    }
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result git_repo_git_repo_list_all_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 0u) {
        snprintf(_err, _err_cap, "git_repo_git_repo_list_all: expected 0 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_list_all: wrong argument count");
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = git_repo_git_repo_list_all(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "git_repo_git_repo_list_all",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "git_repo_git_repo_list_all", call_result);
    }
    {
        const char *_sv = call_result.value ? call_result.value : "";
        cmp_write_str(_mw, _sv, (uint32_t)strlen(_sv));
        free(call_result.value);
    }
    return PICOMESH_OK_VOID();
}

struct object_ptr_result git_repo_git_repo_create(struct ctx *ctx)
{
    ydebug("class=git_repo_git_repo");
    struct class_ptr_result _kr = git_repo_git_repo_class_get();
    if (PICOMESH_IS_ERR(_kr))
        return PICOMESH_ERR(object_ptr, "git_repo_git_repo_create: class accessor failed", _kr);
    /* A service dependency is acquired once and cached for the connection
     * (remote) / process (in-process) lifetime — no per-call create. */
    return rpc_object_acquire(ctx, _kr.value, "git_repo_git_repo");
}


/* ---- git_repo: jinvoke table ------------------------------------ */

struct git_repo_jinvoke_row { const char *name; jinvoke_fn fn; };

static const struct git_repo_jinvoke_row git_repo_jinvoke_rows[] = {
    {"git_repo_git_repo_make", git_repo_git_repo_make_jinvoke},
    {"git_repo_git_repo_delete", git_repo_git_repo_delete_jinvoke},
    {"git_repo_git_repo_owner_of", git_repo_git_repo_owner_of_jinvoke},
    {"git_repo_git_repo_namespace_of", git_repo_git_repo_namespace_of_jinvoke},
    {"git_repo_git_repo_count_for_owner", git_repo_git_repo_count_for_owner_jinvoke},
    {"git_repo_git_repo_count_total", git_repo_git_repo_count_total_jinvoke},
    {"git_repo_git_repo_list_for_owner", git_repo_git_repo_list_for_owner_jinvoke},
    {"git_repo_git_repo_list_for_namespace", git_repo_git_repo_list_for_namespace_jinvoke},
    {"git_repo_git_repo_count_for_namespace", git_repo_git_repo_count_for_namespace_jinvoke},
    {"git_repo_git_repo_read_tree", git_repo_git_repo_read_tree_jinvoke},
    {"git_repo_git_repo_read_file", git_repo_git_repo_read_file_jinvoke},
    {"git_repo_git_repo_put_file", git_repo_git_repo_put_file_jinvoke},
    {"git_repo_git_repo_is_public", git_repo_git_repo_is_public_jinvoke},
    {"git_repo_git_repo_set_public", git_repo_git_repo_set_public_jinvoke},
    {"git_repo_git_repo_list", git_repo_git_repo_list_jinvoke},
    {"git_repo_git_repo_list_all", git_repo_git_repo_list_all_jinvoke}
};

static jinvoke_fn git_repo_jinvoke_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(git_repo_jinvoke_rows) / sizeof(git_repo_jinvoke_rows[0]); ++i)
        if (strcmp(git_repo_jinvoke_rows[i].name, qname) == 0)
            return git_repo_jinvoke_rows[i].fn;
    return NULL;
}

/* ---- git_repo: minvoke table ------------------------------------ */

struct git_repo_minvoke_row { const char *name; minvoke_fn fn; };

static const struct git_repo_minvoke_row git_repo_minvoke_rows[] = {
    {"git_repo_git_repo_make", git_repo_git_repo_make_minvoke},
    {"git_repo_git_repo_delete", git_repo_git_repo_delete_minvoke},
    {"git_repo_git_repo_owner_of", git_repo_git_repo_owner_of_minvoke},
    {"git_repo_git_repo_namespace_of", git_repo_git_repo_namespace_of_minvoke},
    {"git_repo_git_repo_count_for_owner", git_repo_git_repo_count_for_owner_minvoke},
    {"git_repo_git_repo_count_total", git_repo_git_repo_count_total_minvoke},
    {"git_repo_git_repo_list_for_owner", git_repo_git_repo_list_for_owner_minvoke},
    {"git_repo_git_repo_list_for_namespace", git_repo_git_repo_list_for_namespace_minvoke},
    {"git_repo_git_repo_count_for_namespace", git_repo_git_repo_count_for_namespace_minvoke},
    {"git_repo_git_repo_read_tree", git_repo_git_repo_read_tree_minvoke},
    {"git_repo_git_repo_read_file", git_repo_git_repo_read_file_minvoke},
    {"git_repo_git_repo_put_file", git_repo_git_repo_put_file_minvoke},
    {"git_repo_git_repo_is_public", git_repo_git_repo_is_public_minvoke},
    {"git_repo_git_repo_set_public", git_repo_git_repo_set_public_minvoke},
    {"git_repo_git_repo_list", git_repo_git_repo_list_minvoke},
    {"git_repo_git_repo_list_all", git_repo_git_repo_list_all_minvoke}
};

static minvoke_fn git_repo_minvoke_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(git_repo_minvoke_rows) / sizeof(git_repo_minvoke_rows[0]); ++i)
        if (strcmp(git_repo_minvoke_rows[i].name, qname) == 0)
            return git_repo_minvoke_rows[i].fn;
    return NULL;
}

/* ---- git_repo: per-method parameter signatures (runtime reflection) -- */

static const struct jinvoke_param git_repo_git_repo_make_params[] = {
    {"owner_id", "uint32_t"},
    {"owner_name", "const char *"},
    {"repo_name", "const char *"}
};
static const struct jinvoke_param git_repo_git_repo_delete_params[] = {
    {"repo_id", "uint32_t"}
};
static const struct jinvoke_param git_repo_git_repo_owner_of_params[] = {
    {"repo_id", "uint32_t"}
};
static const struct jinvoke_param git_repo_git_repo_namespace_of_params[] = {
    {"repo_id", "uint32_t"}
};
static const struct jinvoke_param git_repo_git_repo_count_for_owner_params[] = {
    {"owner_id", "uint32_t"}
};
static const struct jinvoke_param git_repo_git_repo_list_for_owner_params[] = {
    {"owner_id", "uint32_t"}
};
static const struct jinvoke_param git_repo_git_repo_list_for_namespace_params[] = {
    {"path", "const char *"}
};
static const struct jinvoke_param git_repo_git_repo_count_for_namespace_params[] = {
    {"path", "const char *"}
};
static const struct jinvoke_param git_repo_git_repo_read_tree_params[] = {
    {"repo_id", "uint32_t"},
    {"ref", "const char *"},
    {"path", "const char *"}
};
static const struct jinvoke_param git_repo_git_repo_read_file_params[] = {
    {"repo_id", "uint32_t"},
    {"ref", "const char *"},
    {"path", "const char *"}
};
static const struct jinvoke_param git_repo_git_repo_put_file_params[] = {
    {"repo_id", "uint32_t"},
    {"path", "const char *"},
    {"content", "const char *"},
    {"message", "const char *"},
    {"author_name", "const char *"},
    {"author_email", "const char *"}
};
static const struct jinvoke_param git_repo_git_repo_is_public_params[] = {
    {"repo_id", "uint32_t"}
};
static const struct jinvoke_param git_repo_git_repo_set_public_params[] = {
    {"repo_id", "uint32_t"},
    {"is_public", "int"}
};
static const struct jinvoke_param git_repo_git_repo_list_params[] = {
    {"offset", "int64_t"},
    {"limit", "int64_t"}
};
struct git_repo_params_row { const char *name; struct jinvoke_params params; };

static const struct git_repo_params_row git_repo_params_rows[] = {
    {"git_repo_git_repo_make", {git_repo_git_repo_make_params, 3}},
    {"git_repo_git_repo_delete", {git_repo_git_repo_delete_params, 1}},
    {"git_repo_git_repo_owner_of", {git_repo_git_repo_owner_of_params, 1}},
    {"git_repo_git_repo_namespace_of", {git_repo_git_repo_namespace_of_params, 1}},
    {"git_repo_git_repo_count_for_owner", {git_repo_git_repo_count_for_owner_params, 1}},
    {"git_repo_git_repo_count_total", {NULL, 0}},
    {"git_repo_git_repo_list_for_owner", {git_repo_git_repo_list_for_owner_params, 1}},
    {"git_repo_git_repo_list_for_namespace", {git_repo_git_repo_list_for_namespace_params, 1}},
    {"git_repo_git_repo_count_for_namespace", {git_repo_git_repo_count_for_namespace_params, 1}},
    {"git_repo_git_repo_read_tree", {git_repo_git_repo_read_tree_params, 3}},
    {"git_repo_git_repo_read_file", {git_repo_git_repo_read_file_params, 3}},
    {"git_repo_git_repo_put_file", {git_repo_git_repo_put_file_params, 6}},
    {"git_repo_git_repo_is_public", {git_repo_git_repo_is_public_params, 1}},
    {"git_repo_git_repo_set_public", {git_repo_git_repo_set_public_params, 2}},
    {"git_repo_git_repo_list", {git_repo_git_repo_list_params, 2}},
    {"git_repo_git_repo_list_all", {NULL, 0}}
};

static const struct jinvoke_params *git_repo_params_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(git_repo_params_rows) / sizeof(git_repo_params_rows[0]); ++i)
        if (strcmp(git_repo_params_rows[i].name, qname) == 0)
            return &git_repo_params_rows[i].params;
    return NULL;
}
/* ---- git_repo: class name → accessor (lazy) ---------------------- */

static struct class_ptr_result git_repo_accessor_lookup(const char *name)
{
    if (strcmp(name, "git_repo_git_repo") == 0) return git_repo_git_repo_class_get();
    return PICOMESH_OK(class_ptr, NULL);
}

/* ---- git_repo: slot → skel, name-keyed static data --------------- */

struct git_repo_skel_row { const char *name; rpc_skel_fn fn; };

static const struct git_repo_skel_row git_repo_skel_rows[] = {
    {"git_repo_git_repo_make", git_repo_git_repo_make_skel},
    {"git_repo_git_repo_delete", git_repo_git_repo_delete_skel},
    {"git_repo_git_repo_owner_of", git_repo_git_repo_owner_of_skel},
    {"git_repo_git_repo_namespace_of", git_repo_git_repo_namespace_of_skel},
    {"git_repo_git_repo_count_for_owner", git_repo_git_repo_count_for_owner_skel},
    {"git_repo_git_repo_count_total", git_repo_git_repo_count_total_skel},
    {"git_repo_git_repo_list_for_owner", git_repo_git_repo_list_for_owner_skel},
    {"git_repo_git_repo_list_for_namespace", git_repo_git_repo_list_for_namespace_skel},
    {"git_repo_git_repo_count_for_namespace", git_repo_git_repo_count_for_namespace_skel},
    {"git_repo_git_repo_read_tree", git_repo_git_repo_read_tree_skel},
    {"git_repo_git_repo_read_file", git_repo_git_repo_read_file_skel},
    {"git_repo_git_repo_put_file", git_repo_git_repo_put_file_skel},
    {"git_repo_git_repo_is_public", git_repo_git_repo_is_public_skel},
    {"git_repo_git_repo_set_public", git_repo_git_repo_set_public_skel},
    {"git_repo_git_repo_list", git_repo_git_repo_list_skel},
    {"git_repo_git_repo_list_all", git_repo_git_repo_list_all_skel}
};

static rpc_skel_fn git_repo_skel_lookup(const char *name)
{
    /* rpc_skel_for has already resolved the slot to its qname (the only
     * Result-returning step), so this hook is a pure name→fn lookup that
     * never has to swallow an error. */
    for (size_t i = 0; i < sizeof(git_repo_skel_rows) / sizeof(git_repo_skel_rows[0]); ++i)
        if (strcmp(git_repo_skel_rows[i].name, name) == 0)
            return git_repo_skel_rows[i].fn;
    return NULL;
}

/* ---- git_repo: registration entry point (called from the driver for
 *      config-ACTIVATED plugins only — registration is activation) ---- */

struct picomesh_void_result picomesh_plugin_git_repo_register(void)
{
    struct picomesh_void_result _ar = class_add_accessor_lookup(git_repo_accessor_lookup);
    PICOMESH_RETURN_IF_ERR(picomesh_void, _ar,
                           "picomesh_plugin_git_repo_register: add accessor lookup");
    rpc_add_skel_lookup(git_repo_skel_lookup);
    jinvoke_add_lookup(git_repo_jinvoke_lookup);
    minvoke_add_lookup(git_repo_minvoke_lookup);
    jinvoke_params_add_lookup(git_repo_params_lookup);
    { struct class_ptr_result reg = git_repo_git_repo_class_get();
      PICOMESH_RETURN_IF_ERR(picomesh_void, reg, "git_repo register: prewarm git_repo_git_repo_class_get"); }
    return PICOMESH_OK_VOID();
}
