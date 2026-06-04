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
#include "sharded_storage.internal.h"
#include <limits.h>
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
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.sharded_storage_db_set");
    struct picomesh_int_result _r = sharded_storage_db_set(&_local, _obj, _hdrs, _s1, _s2, _s3);
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
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.sharded_storage_db_get");
    struct picomesh_string_result _r = sharded_storage_db_get(&_local, _obj, _hdrs, _s1, _s2);
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
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.sharded_storage_db_exists");
    struct picomesh_int_result _r = sharded_storage_db_exists(&_local, _obj, _hdrs, _s1, _s2);
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
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.sharded_storage_db_del");
    struct picomesh_int_result _r = sharded_storage_db_del(&_local, _obj, _hdrs, _s1, _s2);
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
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.sharded_storage_db_count");
    struct picomesh_size_result _r = sharded_storage_db_count(&_local, _obj, _hdrs, _s1);
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

static size_t sharded_storage_db_list_skel(const void *_body, size_t _body_len,
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
    int64_t _v3 = 0;
    if (_off + sizeof(_v3) > _body_len) goto _short_body;
    memcpy(&_v3, (const uint8_t *)_body + _off, sizeof(_v3));
    _off += sizeof(_v3);
    int64_t _v4 = 0;
    if (_off + sizeof(_v4) > _body_len) goto _short_body;
    memcpy(&_v4, (const uint8_t *)_body + _off, sizeof(_v4));
    _off += sizeof(_v4);
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.sharded_storage_db_list");
    struct picomesh_json_result _r = sharded_storage_db_list(&_local, _obj, _hdrs, _s1, _s2, _v3, _v4);
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

static size_t sharded_storage_db_list_all_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.sharded_storage_db_list_all");
    struct picomesh_json_result _r = sharded_storage_db_list_all(&_local, _obj, _hdrs, _s1, _s2);
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

static size_t sharded_storage_db_incr_skel(const void *_body, size_t _body_len,
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
    int64_t _v3 = 0;
    if (_off + sizeof(_v3) > _body_len) goto _short_body;
    memcpy(&_v3, (const uint8_t *)_body + _off, sizeof(_v3));
    _off += sizeof(_v3);
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.sharded_storage_db_incr");
    struct picomesh_int64_result _r = sharded_storage_db_incr(&_local, _obj, _hdrs, _s1, _s2, _v3);
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

static size_t sharded_storage_db_put_if_absent_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.sharded_storage_db_put_if_absent");
    struct picomesh_int_result _r = sharded_storage_db_put_if_absent(&_local, _obj, _hdrs, _s1, _s2, _s3);
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

static size_t sharded_storage_db_compare_and_set_skel(const void *_body, size_t _body_len,
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
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.sharded_storage_db_compare_and_set");
    struct picomesh_int_result _r = sharded_storage_db_compare_and_set(&_local, _obj, _hdrs, _s1, _s2, _s3, _s4);
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

static int sharded_storage_db_set_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    const char *arg0 = yjson_as_string(yjson_array_at(args, 0), "");
    const char *arg1 = yjson_as_string(yjson_array_at(args, 1), "");
    const char *arg2 = yjson_as_string(yjson_array_at(args, 2), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = sharded_storage_db_set(call_ctx, obj, hdrs, arg0, arg1, arg2);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "sharded_storage_db_set",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
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
    struct picomesh_string_result call_result = sharded_storage_db_get(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "sharded_storage_db_get",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_string(result, call_result.value ? call_result.value : "");
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
    struct picomesh_int_result call_result = sharded_storage_db_exists(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "sharded_storage_db_exists",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
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
    struct picomesh_int_result call_result = sharded_storage_db_del(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "sharded_storage_db_del",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int sharded_storage_db_count_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    const char *arg0 = yjson_as_string(yjson_array_at(args, 0), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = sharded_storage_db_count(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "sharded_storage_db_count",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int sharded_storage_db_list_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    const char *arg0 = yjson_as_string(yjson_array_at(args, 0), "");
    const char *arg1 = yjson_as_string(yjson_array_at(args, 1), "");
    int64_t arg2 = (int64_t)yjson_as_int(yjson_array_at(args, 2), 0);
    int64_t arg3 = (int64_t)yjson_as_int(yjson_array_at(args, 3), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = sharded_storage_db_list(call_ctx, obj, hdrs, arg0, arg1, arg2, arg3);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "sharded_storage_db_list",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_raw(result, call_result.value ? call_result.value : "null");
    free(call_result.value);
    return 0;
}

static int sharded_storage_db_list_all_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    const char *arg0 = yjson_as_string(yjson_array_at(args, 0), "");
    const char *arg1 = yjson_as_string(yjson_array_at(args, 1), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = sharded_storage_db_list_all(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "sharded_storage_db_list_all",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_raw(result, call_result.value ? call_result.value : "null");
    free(call_result.value);
    return 0;
}

static int sharded_storage_db_incr_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    const char *arg0 = yjson_as_string(yjson_array_at(args, 0), "");
    const char *arg1 = yjson_as_string(yjson_array_at(args, 1), "");
    int64_t arg2 = (int64_t)yjson_as_int(yjson_array_at(args, 2), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = sharded_storage_db_incr(call_ctx, obj, hdrs, arg0, arg1, arg2);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "sharded_storage_db_incr",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int sharded_storage_db_put_if_absent_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    const char *arg0 = yjson_as_string(yjson_array_at(args, 0), "");
    const char *arg1 = yjson_as_string(yjson_array_at(args, 1), "");
    const char *arg2 = yjson_as_string(yjson_array_at(args, 2), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = sharded_storage_db_put_if_absent(call_ctx, obj, hdrs, arg0, arg1, arg2);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "sharded_storage_db_put_if_absent",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int sharded_storage_db_compare_and_set_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{
    const char *arg0 = yjson_as_string(yjson_array_at(args, 0), "");
    const char *arg1 = yjson_as_string(yjson_array_at(args, 1), "");
    const char *arg2 = yjson_as_string(yjson_array_at(args, 2), "");
    const char *arg3 = yjson_as_string(yjson_array_at(args, 3), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = sharded_storage_db_compare_and_set(call_ctx, obj, hdrs, arg0, arg1, arg2, arg3);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "sharded_storage_db_compare_and_set",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    yjson_writer_int(result, (int64_t)call_result.value);
    return 0;
}

static int sharded_storage_db_set_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 3u) {
        snprintf(_err, _err_cap, "sharded_storage_db_set: expected 3 arg(s), got %u", _argc);
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
    struct picomesh_int_result call_result = sharded_storage_db_set(call_ctx, obj, hdrs, _v0, _v1, _v2);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "sharded_storage_db_set",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return 0;
}

static int sharded_storage_db_get_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "sharded_storage_db_get: expected 2 arg(s), got %u", _argc);
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
    struct picomesh_string_result call_result = sharded_storage_db_get(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "sharded_storage_db_get",
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

static int sharded_storage_db_exists_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "sharded_storage_db_exists: expected 2 arg(s), got %u", _argc);
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
    struct picomesh_int_result call_result = sharded_storage_db_exists(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "sharded_storage_db_exists",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return 0;
}

static int sharded_storage_db_del_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "sharded_storage_db_del: expected 2 arg(s), got %u", _argc);
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
    struct picomesh_int_result call_result = sharded_storage_db_del(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "sharded_storage_db_del",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return 0;
}

static int sharded_storage_db_count_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "sharded_storage_db_count: expected 1 arg(s), got %u", _argc);
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
    struct picomesh_size_result call_result = sharded_storage_db_count(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "sharded_storage_db_count",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_uinteger(_mw, (uint64_t)call_result.value);
    return 0;
}

static int sharded_storage_db_list_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 4u) {
        snprintf(_err, _err_cap, "sharded_storage_db_list: expected 4 arg(s), got %u", _argc);
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
            snprintf(_err, _err_cap, "prefix: expected str arg (%s)", cmp_strerror(_mr));
            return -1;
        }
    }
    int64_t _v2;
    if (!cmp_read_integer(_mr, &_v2)) { snprintf(_err, _err_cap, "offset: expected int (%s)", cmp_strerror(_mr)); return -1; }
    int64_t _v3;
    if (!cmp_read_integer(_mr, &_v3)) { snprintf(_err, _err_cap, "limit: expected int (%s)", cmp_strerror(_mr)); return -1; }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = sharded_storage_db_list(call_ctx, obj, hdrs, _v0, _v1, _v2, _v3);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "sharded_storage_db_list",
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

static int sharded_storage_db_list_all_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "sharded_storage_db_list_all: expected 2 arg(s), got %u", _argc);
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
            snprintf(_err, _err_cap, "prefix: expected str arg (%s)", cmp_strerror(_mr));
            return -1;
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = sharded_storage_db_list_all(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "sharded_storage_db_list_all",
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

static int sharded_storage_db_incr_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 3u) {
        snprintf(_err, _err_cap, "sharded_storage_db_incr: expected 3 arg(s), got %u", _argc);
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
    int64_t _v2;
    if (!cmp_read_integer(_mr, &_v2)) { snprintf(_err, _err_cap, "delta: expected int (%s)", cmp_strerror(_mr)); return -1; }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = sharded_storage_db_incr(call_ctx, obj, hdrs, _v0, _v1, _v2);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "sharded_storage_db_incr",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return 0;
}

static int sharded_storage_db_put_if_absent_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 3u) {
        snprintf(_err, _err_cap, "sharded_storage_db_put_if_absent: expected 3 arg(s), got %u", _argc);
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
    struct picomesh_int_result call_result = sharded_storage_db_put_if_absent(call_ctx, obj, hdrs, _v0, _v1, _v2);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "sharded_storage_db_put_if_absent",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return 0;
}

static int sharded_storage_db_compare_and_set_minvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          cmp_ctx_t *_mr, uint32_t _argc, cmp_ctx_t *_mw,
                          char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 4u) {
        snprintf(_err, _err_cap, "sharded_storage_db_compare_and_set: expected 4 arg(s), got %u", _argc);
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
            snprintf(_err, _err_cap, "expected: expected str arg (%s)", cmp_strerror(_mr));
            return -1;
        }
    }
    char _v3[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v3);
        if (!cmp_read_str(_mr, _v3, &_sz)) {
            snprintf(_err, _err_cap, "replacement: expected str arg (%s)", cmp_strerror(_mr));
            return -1;
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = sharded_storage_db_compare_and_set(call_ctx, obj, hdrs, _v0, _v1, _v2, _v3);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "sharded_storage_db_compare_and_set",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        picomesh_error_destroy(call_result.error);
        return -1;
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return 0;
}

struct object_ptr_result sharded_storage_db_create(struct ctx *ctx)
{
    ydebug("class=sharded_storage_db");
    struct class_ptr_result _kr = sharded_storage_db_class_get();
    if (PICOMESH_IS_ERR(_kr))
        return PICOMESH_ERR(object_ptr, "sharded_storage_db_create: class accessor failed", _kr);
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
    {"sharded_storage_db_count", sharded_storage_db_count_jinvoke},
    {"sharded_storage_db_list", sharded_storage_db_list_jinvoke},
    {"sharded_storage_db_list_all", sharded_storage_db_list_all_jinvoke},
    {"sharded_storage_db_incr", sharded_storage_db_incr_jinvoke},
    {"sharded_storage_db_put_if_absent", sharded_storage_db_put_if_absent_jinvoke},
    {"sharded_storage_db_compare_and_set", sharded_storage_db_compare_and_set_jinvoke}
};

static jinvoke_fn sharded_storage_jinvoke_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(sharded_storage_jinvoke_rows) / sizeof(sharded_storage_jinvoke_rows[0]); ++i)
        if (strcmp(sharded_storage_jinvoke_rows[i].name, qname) == 0)
            return sharded_storage_jinvoke_rows[i].fn;
    return NULL;
}

/* ---- sharded_storage: minvoke table ------------------------------------ */

struct sharded_storage_minvoke_row { const char *name; minvoke_fn fn; };

static const struct sharded_storage_minvoke_row sharded_storage_minvoke_rows[] = {
    {"sharded_storage_db_set", sharded_storage_db_set_minvoke},
    {"sharded_storage_db_get", sharded_storage_db_get_minvoke},
    {"sharded_storage_db_exists", sharded_storage_db_exists_minvoke},
    {"sharded_storage_db_del", sharded_storage_db_del_minvoke},
    {"sharded_storage_db_count", sharded_storage_db_count_minvoke},
    {"sharded_storage_db_list", sharded_storage_db_list_minvoke},
    {"sharded_storage_db_list_all", sharded_storage_db_list_all_minvoke},
    {"sharded_storage_db_incr", sharded_storage_db_incr_minvoke},
    {"sharded_storage_db_put_if_absent", sharded_storage_db_put_if_absent_minvoke},
    {"sharded_storage_db_compare_and_set", sharded_storage_db_compare_and_set_minvoke}
};

static minvoke_fn sharded_storage_minvoke_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(sharded_storage_minvoke_rows) / sizeof(sharded_storage_minvoke_rows[0]); ++i)
        if (strcmp(sharded_storage_minvoke_rows[i].name, qname) == 0)
            return sharded_storage_minvoke_rows[i].fn;
    return NULL;
}

/* ---- sharded_storage: per-method parameter signatures (runtime reflection) -- */

static const struct jinvoke_param sharded_storage_db_set_params[] = {
    {"context", "const char *"},
    {"key", "const char *"},
    {"value", "const char *"}
};
static const struct jinvoke_param sharded_storage_db_get_params[] = {
    {"context", "const char *"},
    {"key", "const char *"}
};
static const struct jinvoke_param sharded_storage_db_exists_params[] = {
    {"context", "const char *"},
    {"key", "const char *"}
};
static const struct jinvoke_param sharded_storage_db_del_params[] = {
    {"context", "const char *"},
    {"key", "const char *"}
};
static const struct jinvoke_param sharded_storage_db_count_params[] = {
    {"context", "const char *"}
};
static const struct jinvoke_param sharded_storage_db_list_params[] = {
    {"context", "const char *"},
    {"prefix", "const char *"},
    {"offset", "int64_t"},
    {"limit", "int64_t"}
};
static const struct jinvoke_param sharded_storage_db_list_all_params[] = {
    {"context", "const char *"},
    {"prefix", "const char *"}
};
static const struct jinvoke_param sharded_storage_db_incr_params[] = {
    {"context", "const char *"},
    {"key", "const char *"},
    {"delta", "int64_t"}
};
static const struct jinvoke_param sharded_storage_db_put_if_absent_params[] = {
    {"context", "const char *"},
    {"key", "const char *"},
    {"value", "const char *"}
};
static const struct jinvoke_param sharded_storage_db_compare_and_set_params[] = {
    {"context", "const char *"},
    {"key", "const char *"},
    {"expected", "const char *"},
    {"replacement", "const char *"}
};
struct sharded_storage_params_row { const char *name; struct jinvoke_params params; };

static const struct sharded_storage_params_row sharded_storage_params_rows[] = {
    {"sharded_storage_db_set", {sharded_storage_db_set_params, 3}},
    {"sharded_storage_db_get", {sharded_storage_db_get_params, 2}},
    {"sharded_storage_db_exists", {sharded_storage_db_exists_params, 2}},
    {"sharded_storage_db_del", {sharded_storage_db_del_params, 2}},
    {"sharded_storage_db_count", {sharded_storage_db_count_params, 1}},
    {"sharded_storage_db_list", {sharded_storage_db_list_params, 4}},
    {"sharded_storage_db_list_all", {sharded_storage_db_list_all_params, 2}},
    {"sharded_storage_db_incr", {sharded_storage_db_incr_params, 3}},
    {"sharded_storage_db_put_if_absent", {sharded_storage_db_put_if_absent_params, 3}},
    {"sharded_storage_db_compare_and_set", {sharded_storage_db_compare_and_set_params, 4}}
};

static const struct jinvoke_params *sharded_storage_params_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(sharded_storage_params_rows) / sizeof(sharded_storage_params_rows[0]); ++i)
        if (strcmp(sharded_storage_params_rows[i].name, qname) == 0)
            return &sharded_storage_params_rows[i].params;
    return NULL;
}
/* ---- sharded_storage: class name → accessor (lazy) ---------------------- */

static struct class_ptr_result sharded_storage_accessor_lookup(const char *name)
{
    if (strcmp(name, "sharded_storage_db") == 0) return sharded_storage_db_class_get();
    return PICOMESH_OK(class_ptr, NULL);
}

/* ---- sharded_storage: slot → skel, name-keyed static data --------------- */

struct sharded_storage_skel_row { const char *name; rpc_skel_fn fn; };

static const struct sharded_storage_skel_row sharded_storage_skel_rows[] = {
    {"sharded_storage_db_set", sharded_storage_db_set_skel},
    {"sharded_storage_db_get", sharded_storage_db_get_skel},
    {"sharded_storage_db_exists", sharded_storage_db_exists_skel},
    {"sharded_storage_db_del", sharded_storage_db_del_skel},
    {"sharded_storage_db_count", sharded_storage_db_count_skel},
    {"sharded_storage_db_list", sharded_storage_db_list_skel},
    {"sharded_storage_db_list_all", sharded_storage_db_list_all_skel},
    {"sharded_storage_db_incr", sharded_storage_db_incr_skel},
    {"sharded_storage_db_put_if_absent", sharded_storage_db_put_if_absent_skel},
    {"sharded_storage_db_compare_and_set", sharded_storage_db_compare_and_set_skel}
};

static rpc_skel_fn sharded_storage_skel_lookup(method_slot slot)
{
    struct const_char_ptr_result nr = method_slot_name(slot);
    if (PICOMESH_IS_ERR(nr)) { picomesh_error_destroy(nr.error); return NULL; }
    const char *name = nr.value;
    for (size_t i = 0; i < sizeof(sharded_storage_skel_rows) / sizeof(sharded_storage_skel_rows[0]); ++i)
        if (strcmp(sharded_storage_skel_rows[i].name, name) == 0)
            return sharded_storage_skel_rows[i].fn;
    return NULL;
}

/* ---- sharded_storage: registration entry point (called from the driver for
 *      config-ACTIVATED plugins only — registration is activation) ---- */

void picomesh_plugin_sharded_storage_register(void)
{
    struct picomesh_void_result _ar = class_add_accessor_lookup(sharded_storage_accessor_lookup);
    if (PICOMESH_IS_ERR(_ar)) {
        picomesh_error_print(stderr, "picomesh_plugin_sharded_storage_register", _ar.error);
        picomesh_error_destroy(_ar.error);
        abort();
    }
    rpc_add_skel_lookup(sharded_storage_skel_lookup);
    jinvoke_add_lookup(sharded_storage_jinvoke_lookup);
    minvoke_add_lookup(sharded_storage_minvoke_lookup);
    jinvoke_params_add_lookup(sharded_storage_params_lookup);
    { struct class_ptr_result reg = sharded_storage_db_class_get();
      if (PICOMESH_IS_ERR(reg)) picomesh_error_destroy(reg.error); }
}
