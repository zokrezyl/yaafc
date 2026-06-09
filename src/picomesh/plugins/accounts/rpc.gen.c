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
#include "accounts.internal.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct picomesh_size_result accounts_accounts_claim_username_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.accounts_accounts_claim_username");
    struct picomesh_int_result _r = accounts_accounts_claim_username(&_local, _obj, _hdrs, _v1, _s2);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "accounts_accounts_claim_username_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "accounts_accounts_claim_username_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result accounts_accounts_release_username_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.accounts_accounts_release_username");
    struct picomesh_int_result _r = accounts_accounts_release_username(&_local, _obj, _hdrs, _v1, _s2);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "accounts_accounts_release_username_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "accounts_accounts_release_username_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result accounts_accounts_allocate_uid_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.accounts_accounts_allocate_uid");
    struct picomesh_int64_result _r = accounts_accounts_allocate_uid(&_local, _obj, _hdrs);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "accounts_accounts_allocate_uid_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "accounts_accounts_allocate_uid_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result accounts_accounts_uid_for_username_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.accounts_accounts_uid_for_username");
    struct picomesh_int64_result _r = accounts_accounts_uid_for_username(&_local, _obj, _hdrs, _s1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "accounts_accounts_uid_for_username_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "accounts_accounts_uid_for_username_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result accounts_accounts_register_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.accounts_accounts_register");
    struct picomesh_int_result _r = accounts_accounts_register(&_local, _obj, _hdrs, _v1, _s2);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "accounts_accounts_register_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "accounts_accounts_register_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result accounts_accounts_exists_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.accounts_accounts_exists");
    struct picomesh_int_result _r = accounts_accounts_exists(&_local, _obj, _hdrs, _v1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "accounts_accounts_exists_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "accounts_accounts_exists_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result accounts_accounts_set_balance_skel(const void *_body, size_t _body_len,
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
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.accounts_accounts_set_balance");
    struct picomesh_int_result _r = accounts_accounts_set_balance(&_local, _obj, _hdrs, _v1, _v2);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "accounts_accounts_set_balance_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "accounts_accounts_set_balance_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result accounts_accounts_balance_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.accounts_accounts_balance");
    struct picomesh_int64_result _r = accounts_accounts_balance(&_local, _obj, _hdrs, _v1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "accounts_accounts_balance_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "accounts_accounts_balance_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result accounts_accounts_count_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.accounts_accounts_count");
    struct picomesh_size_result _r = accounts_accounts_count(&_local, _obj, _hdrs);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "accounts_accounts_count_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "accounts_accounts_count_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result accounts_accounts_set_groups_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.accounts_accounts_set_groups");
    struct picomesh_int_result _r = accounts_accounts_set_groups(&_local, _obj, _hdrs, _v1, _s2);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "accounts_accounts_set_groups_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "accounts_accounts_set_groups_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result accounts_accounts_groups_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.accounts_accounts_groups");
    struct picomesh_string_result _r = accounts_accounts_groups(&_local, _obj, _hdrs, _v1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "accounts_accounts_groups_skel: response buffer too small");
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
        if (_resp_max < 1 + 4 + (size_t)_svlen) { free(_r.value); return PICOMESH_ERR(picomesh_size, "accounts_accounts_groups_skel: response buffer too small"); }
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

static struct picomesh_size_result accounts_accounts_ns_create_skel(const void *_body, size_t _body_len,
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
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.accounts_accounts_ns_create");
    struct picomesh_string_result _r = accounts_accounts_ns_create(&_local, _obj, _hdrs, _v1, _s2, _s3, _s4);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "accounts_accounts_ns_create_skel: response buffer too small");
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
        if (_resp_max < 1 + 4 + (size_t)_svlen) { free(_r.value); return PICOMESH_ERR(picomesh_size, "accounts_accounts_ns_create_skel: response buffer too small"); }
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

static struct picomesh_size_result accounts_accounts_ns_add_member_skel(const void *_body, size_t _body_len,
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
    uint32_t _v2 = 0;
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.accounts_accounts_ns_add_member");
    struct picomesh_int_result _r = accounts_accounts_ns_add_member(&_local, _obj, _hdrs, _s1, _v2, _s3);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "accounts_accounts_ns_add_member_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "accounts_accounts_ns_add_member_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result accounts_accounts_ns_resolve_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.accounts_accounts_ns_resolve");
    struct picomesh_int64_result _r = accounts_accounts_ns_resolve(&_local, _obj, _hdrs, _s1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "accounts_accounts_ns_resolve_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "accounts_accounts_ns_resolve_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result accounts_accounts_ns_list_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.accounts_accounts_ns_list");
    struct picomesh_json_result _r = accounts_accounts_ns_list(&_local, _obj, _hdrs);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "accounts_accounts_ns_list_skel: response buffer too small");
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
        if (_resp_max < 1 + 4 + (size_t)_svlen) { free(_r.value); return PICOMESH_ERR(picomesh_size, "accounts_accounts_ns_list_skel: response buffer too small"); }
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

static struct picomesh_size_result accounts_accounts_ns_members_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.accounts_accounts_ns_members");
    struct picomesh_json_result _r = accounts_accounts_ns_members(&_local, _obj, _hdrs, _s1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "accounts_accounts_ns_members_skel: response buffer too small");
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
        if (_resp_max < 1 + 4 + (size_t)_svlen) { free(_r.value); return PICOMESH_ERR(picomesh_size, "accounts_accounts_ns_members_skel: response buffer too small"); }
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

static struct picomesh_size_result accounts_accounts_ns_remove_member_skel(const void *_body, size_t _body_len,
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
    uint32_t _v2 = 0;
    if (_off + sizeof(_v2) > _body_len) goto _short_body;
    memcpy(&_v2, (const uint8_t *)_body + _off, sizeof(_v2));
    _off += sizeof(_v2);
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.accounts_accounts_ns_remove_member");
    struct picomesh_int_result _r = accounts_accounts_ns_remove_member(&_local, _obj, _hdrs, _s1, _v2);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "accounts_accounts_ns_remove_member_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "accounts_accounts_ns_remove_member_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result accounts_accounts_ns_subtree_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.accounts_accounts_ns_subtree");
    struct picomesh_json_result _r = accounts_accounts_ns_subtree(&_local, _obj, _hdrs, _s1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "accounts_accounts_ns_subtree_skel: response buffer too small");
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
        if (_resp_max < 1 + 4 + (size_t)_svlen) { free(_r.value); return PICOMESH_ERR(picomesh_size, "accounts_accounts_ns_subtree_skel: response buffer too small"); }
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

static struct picomesh_size_result accounts_accounts_ns_delete_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.accounts_accounts_ns_delete");
    struct picomesh_int_result _r = accounts_accounts_ns_delete(&_local, _obj, _hdrs, _s1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "accounts_accounts_ns_delete_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "accounts_accounts_ns_delete_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result accounts_accounts_list_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.accounts_accounts_list");
    struct picomesh_json_result _r = accounts_accounts_list(&_local, _obj, _hdrs, _v1, _v2);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "accounts_accounts_list_skel: response buffer too small");
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
        if (_resp_max < 1 + 4 + (size_t)_svlen) { free(_r.value); return PICOMESH_ERR(picomesh_size, "accounts_accounts_list_skel: response buffer too small"); }
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

static struct picomesh_size_result accounts_accounts_list_all_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.accounts_accounts_list_all");
    struct picomesh_json_result _r = accounts_accounts_list_all(&_local, _obj, _hdrs);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "accounts_accounts_list_all_skel: response buffer too small");
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
        if (_resp_max < 1 + 4 + (size_t)_svlen) { free(_r.value); return PICOMESH_ERR(picomesh_size, "accounts_accounts_list_all_skel: response buffer too small"); }
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

static struct picomesh_void_result accounts_accounts_claim_username_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] accounts_accounts_claim_username");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    const char *arg1 = json_as_string(json_array_at(args, 1), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = accounts_accounts_claim_username(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "accounts_accounts_claim_username",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_claim_username", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_release_username_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] accounts_accounts_release_username");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    const char *arg1 = json_as_string(json_array_at(args, 1), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = accounts_accounts_release_username(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "accounts_accounts_release_username",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_release_username", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_allocate_uid_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] accounts_accounts_allocate_uid");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = accounts_accounts_allocate_uid(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "accounts_accounts_allocate_uid",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_allocate_uid", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_uid_for_username_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] accounts_accounts_uid_for_username");
    const char *arg0 = json_as_string(json_array_at(args, 0), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = accounts_accounts_uid_for_username(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "accounts_accounts_uid_for_username",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_uid_for_username", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_register_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] accounts_accounts_register");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    const char *arg1 = json_as_string(json_array_at(args, 1), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = accounts_accounts_register(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "accounts_accounts_register",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_register", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_exists_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] accounts_accounts_exists");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = accounts_accounts_exists(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "accounts_accounts_exists",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_exists", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_set_balance_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] accounts_accounts_set_balance");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    int64_t arg1 = (int64_t)json_as_int(json_array_at(args, 1), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = accounts_accounts_set_balance(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "accounts_accounts_set_balance",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_set_balance", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_balance_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] accounts_accounts_balance");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = accounts_accounts_balance(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "accounts_accounts_balance",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_balance", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_count_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] accounts_accounts_count");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = accounts_accounts_count(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "accounts_accounts_count",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_count", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_set_groups_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] accounts_accounts_set_groups");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    const char *arg1 = json_as_string(json_array_at(args, 1), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = accounts_accounts_set_groups(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "accounts_accounts_set_groups",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_set_groups", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_groups_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] accounts_accounts_groups");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_string_result call_result = accounts_accounts_groups(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "accounts_accounts_groups",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_groups", call_result);
    }
    json_writer_string(result, call_result.value ? call_result.value : "");
    free(call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_ns_create_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] accounts_accounts_ns_create");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    const char *arg1 = json_as_string(json_array_at(args, 1), "");
    const char *arg2 = json_as_string(json_array_at(args, 2), "");
    const char *arg3 = json_as_string(json_array_at(args, 3), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_string_result call_result = accounts_accounts_ns_create(call_ctx, obj, hdrs, arg0, arg1, arg2, arg3);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "accounts_accounts_ns_create",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_create", call_result);
    }
    json_writer_string(result, call_result.value ? call_result.value : "");
    free(call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_ns_add_member_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] accounts_accounts_ns_add_member");
    const char *arg0 = json_as_string(json_array_at(args, 0), "");
    uint32_t arg1 = (uint32_t)json_as_int(json_array_at(args, 1), 0);
    const char *arg2 = json_as_string(json_array_at(args, 2), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = accounts_accounts_ns_add_member(call_ctx, obj, hdrs, arg0, arg1, arg2);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "accounts_accounts_ns_add_member",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_add_member", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_ns_resolve_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] accounts_accounts_ns_resolve");
    const char *arg0 = json_as_string(json_array_at(args, 0), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = accounts_accounts_ns_resolve(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "accounts_accounts_ns_resolve",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_resolve", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_ns_list_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] accounts_accounts_ns_list");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = accounts_accounts_ns_list(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "accounts_accounts_ns_list",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_list", call_result);
    }
    json_writer_raw(result, call_result.value ? call_result.value : "null");
    free(call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_ns_members_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] accounts_accounts_ns_members");
    const char *arg0 = json_as_string(json_array_at(args, 0), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = accounts_accounts_ns_members(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "accounts_accounts_ns_members",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_members", call_result);
    }
    json_writer_raw(result, call_result.value ? call_result.value : "null");
    free(call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_ns_remove_member_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] accounts_accounts_ns_remove_member");
    const char *arg0 = json_as_string(json_array_at(args, 0), "");
    uint32_t arg1 = (uint32_t)json_as_int(json_array_at(args, 1), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = accounts_accounts_ns_remove_member(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "accounts_accounts_ns_remove_member",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_remove_member", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_ns_subtree_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] accounts_accounts_ns_subtree");
    const char *arg0 = json_as_string(json_array_at(args, 0), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = accounts_accounts_ns_subtree(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "accounts_accounts_ns_subtree",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_subtree", call_result);
    }
    json_writer_raw(result, call_result.value ? call_result.value : "null");
    free(call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_ns_delete_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] accounts_accounts_ns_delete");
    const char *arg0 = json_as_string(json_array_at(args, 0), "");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = accounts_accounts_ns_delete(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "accounts_accounts_ns_delete",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_delete", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_list_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] accounts_accounts_list");
    int64_t arg0 = (int64_t)json_as_int(json_array_at(args, 0), 0);
    int64_t arg1 = (int64_t)json_as_int(json_array_at(args, 1), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = accounts_accounts_list(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "accounts_accounts_list",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_list", call_result);
    }
    json_writer_raw(result, call_result.value ? call_result.value : "null");
    free(call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_list_all_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] accounts_accounts_list_all");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = accounts_accounts_list_all(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "accounts_accounts_list_all",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_list_all", call_result);
    }
    json_writer_raw(result, call_result.value ? call_result.value : "null");
    free(call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_claim_username_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "accounts_accounts_claim_username: expected 2 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_claim_username: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "uid: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "uid: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    char _v1[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v1);
        if (!cmp_read_str(_mr, _v1, &_sz)) {
            snprintf(_err, _err_cap, "username: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = accounts_accounts_claim_username(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "accounts_accounts_claim_username",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_claim_username", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_release_username_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "accounts_accounts_release_username: expected 2 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_release_username: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "uid: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "uid: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    char _v1[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v1);
        if (!cmp_read_str(_mr, _v1, &_sz)) {
            snprintf(_err, _err_cap, "username: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = accounts_accounts_release_username(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "accounts_accounts_release_username",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_release_username", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_allocate_uid_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 0u) {
        snprintf(_err, _err_cap, "accounts_accounts_allocate_uid: expected 0 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_allocate_uid: wrong argument count");
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = accounts_accounts_allocate_uid(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "accounts_accounts_allocate_uid",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_allocate_uid", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_uid_for_username_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "accounts_accounts_uid_for_username: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_uid_for_username: wrong argument count");
    }
    char _v0[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v0);
        if (!cmp_read_str(_mr, _v0, &_sz)) {
            snprintf(_err, _err_cap, "username: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = accounts_accounts_uid_for_username(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "accounts_accounts_uid_for_username",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_uid_for_username", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_register_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "accounts_accounts_register: expected 2 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_register: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "uid: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "uid: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    char _v1[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v1);
        if (!cmp_read_str(_mr, _v1, &_sz)) {
            snprintf(_err, _err_cap, "username: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = accounts_accounts_register(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "accounts_accounts_register",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_register", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_exists_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "accounts_accounts_exists: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_exists: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "uid: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "uid: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = accounts_accounts_exists(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "accounts_accounts_exists",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_exists", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_set_balance_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "accounts_accounts_set_balance: expected 2 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_set_balance: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "uid: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "uid: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    int64_t _v1;
    if (!cmp_read_integer(_mr, &_v1)) { snprintf(_err, _err_cap, "n: expected int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = accounts_accounts_set_balance(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "accounts_accounts_set_balance",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_set_balance", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_balance_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "accounts_accounts_balance: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_balance: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "uid: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "uid: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = accounts_accounts_balance(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "accounts_accounts_balance",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_balance", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_count_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 0u) {
        snprintf(_err, _err_cap, "accounts_accounts_count: expected 0 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_count: wrong argument count");
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = accounts_accounts_count(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "accounts_accounts_count",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_count", call_result);
    }
    cmp_write_uinteger(_mw, (uint64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_set_groups_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "accounts_accounts_set_groups: expected 2 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_set_groups: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "uid: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "uid: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    char _v1[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v1);
        if (!cmp_read_str(_mr, _v1, &_sz)) {
            snprintf(_err, _err_cap, "groups_csv: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = accounts_accounts_set_groups(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "accounts_accounts_set_groups",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_set_groups", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_groups_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "accounts_accounts_groups: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_groups: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "uid: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "uid: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_string_result call_result = accounts_accounts_groups(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "accounts_accounts_groups",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_groups", call_result);
    }
    {
        const char *_sv = call_result.value ? call_result.value : "";
        cmp_write_str(_mw, _sv, (uint32_t)strlen(_sv));
        free(call_result.value);
    }
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_ns_create_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 4u) {
        snprintf(_err, _err_cap, "accounts_accounts_ns_create: expected 4 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_create: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "owner_uid: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "owner_uid: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    char _v1[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v1);
        if (!cmp_read_str(_mr, _v1, &_sz)) {
            snprintf(_err, _err_cap, "kind: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    char _v2[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v2);
        if (!cmp_read_str(_mr, _v2, &_sz)) {
            snprintf(_err, _err_cap, "slug: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    char _v3[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v3);
        if (!cmp_read_str(_mr, _v3, &_sz)) {
            snprintf(_err, _err_cap, "parent_path: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_string_result call_result = accounts_accounts_ns_create(call_ctx, obj, hdrs, _v0, _v1, _v2, _v3);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "accounts_accounts_ns_create",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_create", call_result);
    }
    {
        const char *_sv = call_result.value ? call_result.value : "";
        cmp_write_str(_mw, _sv, (uint32_t)strlen(_sv));
        free(call_result.value);
    }
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_ns_add_member_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 3u) {
        snprintf(_err, _err_cap, "accounts_accounts_ns_add_member: expected 3 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_add_member: wrong argument count");
    }
    char _v0[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v0);
        if (!cmp_read_str(_mr, _v0, &_sz)) {
            snprintf(_err, _err_cap, "path: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    uint32_t _v1;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "uid: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "uid: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v1 = (uint32_t)_u;
    }
    char _v2[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v2);
        if (!cmp_read_str(_mr, _v2, &_sz)) {
            snprintf(_err, _err_cap, "role: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = accounts_accounts_ns_add_member(call_ctx, obj, hdrs, _v0, _v1, _v2);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "accounts_accounts_ns_add_member",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_add_member", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_ns_resolve_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "accounts_accounts_ns_resolve: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_resolve: wrong argument count");
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
    struct picomesh_int64_result call_result = accounts_accounts_ns_resolve(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "accounts_accounts_ns_resolve",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_resolve", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_ns_list_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 0u) {
        snprintf(_err, _err_cap, "accounts_accounts_ns_list: expected 0 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_list: wrong argument count");
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = accounts_accounts_ns_list(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "accounts_accounts_ns_list",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_list", call_result);
    }
    {
        const char *_sv = call_result.value ? call_result.value : "";
        cmp_write_str(_mw, _sv, (uint32_t)strlen(_sv));
        free(call_result.value);
    }
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_ns_members_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "accounts_accounts_ns_members: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_members: wrong argument count");
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
    struct picomesh_json_result call_result = accounts_accounts_ns_members(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "accounts_accounts_ns_members",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_members", call_result);
    }
    {
        const char *_sv = call_result.value ? call_result.value : "";
        cmp_write_str(_mw, _sv, (uint32_t)strlen(_sv));
        free(call_result.value);
    }
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_ns_remove_member_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "accounts_accounts_ns_remove_member: expected 2 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_remove_member: wrong argument count");
    }
    char _v0[4096];
    {
        uint32_t _sz = (uint32_t)sizeof(_v0);
        if (!cmp_read_str(_mr, _v0, &_sz)) {
            snprintf(_err, _err_cap, "path: expected str arg (%s)", cmp_strerror(_mr));
            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");
        }
    }
    uint32_t _v1;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "uid: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "uid: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v1 = (uint32_t)_u;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = accounts_accounts_ns_remove_member(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "accounts_accounts_ns_remove_member",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_remove_member", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_ns_subtree_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "accounts_accounts_ns_subtree: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_subtree: wrong argument count");
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
    struct picomesh_json_result call_result = accounts_accounts_ns_subtree(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "accounts_accounts_ns_subtree",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_subtree", call_result);
    }
    {
        const char *_sv = call_result.value ? call_result.value : "";
        cmp_write_str(_mw, _sv, (uint32_t)strlen(_sv));
        free(call_result.value);
    }
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_ns_delete_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "accounts_accounts_ns_delete: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_delete: wrong argument count");
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
    struct picomesh_int_result call_result = accounts_accounts_ns_delete(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "accounts_accounts_ns_delete",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_ns_delete", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_list_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "accounts_accounts_list: expected 2 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_list: wrong argument count");
    }
    int64_t _v0;
    if (!cmp_read_integer(_mr, &_v0)) { snprintf(_err, _err_cap, "offset: expected int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
    int64_t _v1;
    if (!cmp_read_integer(_mr, &_v1)) { snprintf(_err, _err_cap, "limit: expected int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = accounts_accounts_list(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "accounts_accounts_list",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_list", call_result);
    }
    {
        const char *_sv = call_result.value ? call_result.value : "";
        cmp_write_str(_mw, _sv, (uint32_t)strlen(_sv));
        free(call_result.value);
    }
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result accounts_accounts_list_all_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 0u) {
        snprintf(_err, _err_cap, "accounts_accounts_list_all: expected 0 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_list_all: wrong argument count");
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_json_result call_result = accounts_accounts_list_all(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "accounts_accounts_list_all",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "accounts_accounts_list_all", call_result);
    }
    {
        const char *_sv = call_result.value ? call_result.value : "";
        cmp_write_str(_mw, _sv, (uint32_t)strlen(_sv));
        free(call_result.value);
    }
    return PICOMESH_OK_VOID();
}

struct object_ptr_result accounts_accounts_create(struct ctx *ctx)
{
    ydebug("class=accounts_accounts");
    struct class_ptr_result _kr = accounts_accounts_class_get();
    if (PICOMESH_IS_ERR(_kr))
        return PICOMESH_ERR(object_ptr, "accounts_accounts_create: class accessor failed", _kr);
    /* A service dependency is acquired once and cached for the connection
     * (remote) / process (in-process) lifetime — no per-call create. */
    return rpc_object_acquire(ctx, _kr.value, "accounts_accounts");
}


/* ---- accounts: jinvoke table ------------------------------------ */

struct accounts_jinvoke_row { const char *name; jinvoke_fn fn; };

static const struct accounts_jinvoke_row accounts_jinvoke_rows[] = {
    {"accounts_accounts_claim_username", accounts_accounts_claim_username_jinvoke},
    {"accounts_accounts_release_username", accounts_accounts_release_username_jinvoke},
    {"accounts_accounts_allocate_uid", accounts_accounts_allocate_uid_jinvoke},
    {"accounts_accounts_uid_for_username", accounts_accounts_uid_for_username_jinvoke},
    {"accounts_accounts_register", accounts_accounts_register_jinvoke},
    {"accounts_accounts_exists", accounts_accounts_exists_jinvoke},
    {"accounts_accounts_set_balance", accounts_accounts_set_balance_jinvoke},
    {"accounts_accounts_balance", accounts_accounts_balance_jinvoke},
    {"accounts_accounts_count", accounts_accounts_count_jinvoke},
    {"accounts_accounts_set_groups", accounts_accounts_set_groups_jinvoke},
    {"accounts_accounts_groups", accounts_accounts_groups_jinvoke},
    {"accounts_accounts_ns_create", accounts_accounts_ns_create_jinvoke},
    {"accounts_accounts_ns_add_member", accounts_accounts_ns_add_member_jinvoke},
    {"accounts_accounts_ns_resolve", accounts_accounts_ns_resolve_jinvoke},
    {"accounts_accounts_ns_list", accounts_accounts_ns_list_jinvoke},
    {"accounts_accounts_ns_members", accounts_accounts_ns_members_jinvoke},
    {"accounts_accounts_ns_remove_member", accounts_accounts_ns_remove_member_jinvoke},
    {"accounts_accounts_ns_subtree", accounts_accounts_ns_subtree_jinvoke},
    {"accounts_accounts_ns_delete", accounts_accounts_ns_delete_jinvoke},
    {"accounts_accounts_list", accounts_accounts_list_jinvoke},
    {"accounts_accounts_list_all", accounts_accounts_list_all_jinvoke}
};

static jinvoke_fn accounts_jinvoke_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(accounts_jinvoke_rows) / sizeof(accounts_jinvoke_rows[0]); ++i)
        if (strcmp(accounts_jinvoke_rows[i].name, qname) == 0)
            return accounts_jinvoke_rows[i].fn;
    return NULL;
}

/* ---- accounts: minvoke table ------------------------------------ */

struct accounts_minvoke_row { const char *name; minvoke_fn fn; };

static const struct accounts_minvoke_row accounts_minvoke_rows[] = {
    {"accounts_accounts_claim_username", accounts_accounts_claim_username_minvoke},
    {"accounts_accounts_release_username", accounts_accounts_release_username_minvoke},
    {"accounts_accounts_allocate_uid", accounts_accounts_allocate_uid_minvoke},
    {"accounts_accounts_uid_for_username", accounts_accounts_uid_for_username_minvoke},
    {"accounts_accounts_register", accounts_accounts_register_minvoke},
    {"accounts_accounts_exists", accounts_accounts_exists_minvoke},
    {"accounts_accounts_set_balance", accounts_accounts_set_balance_minvoke},
    {"accounts_accounts_balance", accounts_accounts_balance_minvoke},
    {"accounts_accounts_count", accounts_accounts_count_minvoke},
    {"accounts_accounts_set_groups", accounts_accounts_set_groups_minvoke},
    {"accounts_accounts_groups", accounts_accounts_groups_minvoke},
    {"accounts_accounts_ns_create", accounts_accounts_ns_create_minvoke},
    {"accounts_accounts_ns_add_member", accounts_accounts_ns_add_member_minvoke},
    {"accounts_accounts_ns_resolve", accounts_accounts_ns_resolve_minvoke},
    {"accounts_accounts_ns_list", accounts_accounts_ns_list_minvoke},
    {"accounts_accounts_ns_members", accounts_accounts_ns_members_minvoke},
    {"accounts_accounts_ns_remove_member", accounts_accounts_ns_remove_member_minvoke},
    {"accounts_accounts_ns_subtree", accounts_accounts_ns_subtree_minvoke},
    {"accounts_accounts_ns_delete", accounts_accounts_ns_delete_minvoke},
    {"accounts_accounts_list", accounts_accounts_list_minvoke},
    {"accounts_accounts_list_all", accounts_accounts_list_all_minvoke}
};

static minvoke_fn accounts_minvoke_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(accounts_minvoke_rows) / sizeof(accounts_minvoke_rows[0]); ++i)
        if (strcmp(accounts_minvoke_rows[i].name, qname) == 0)
            return accounts_minvoke_rows[i].fn;
    return NULL;
}

/* ---- accounts: per-method parameter signatures (runtime reflection) -- */

static const struct jinvoke_param accounts_accounts_claim_username_params[] = {
    {"uid", "uint32_t"},
    {"username", "const char *"}
};
static const struct jinvoke_param accounts_accounts_release_username_params[] = {
    {"uid", "uint32_t"},
    {"username", "const char *"}
};
static const struct jinvoke_param accounts_accounts_uid_for_username_params[] = {
    {"username", "const char *"}
};
static const struct jinvoke_param accounts_accounts_register_params[] = {
    {"uid", "uint32_t"},
    {"username", "const char *"}
};
static const struct jinvoke_param accounts_accounts_exists_params[] = {
    {"uid", "uint32_t"}
};
static const struct jinvoke_param accounts_accounts_set_balance_params[] = {
    {"uid", "uint32_t"},
    {"n", "int64_t"}
};
static const struct jinvoke_param accounts_accounts_balance_params[] = {
    {"uid", "uint32_t"}
};
static const struct jinvoke_param accounts_accounts_set_groups_params[] = {
    {"uid", "uint32_t"},
    {"groups_csv", "const char *"}
};
static const struct jinvoke_param accounts_accounts_groups_params[] = {
    {"uid", "uint32_t"}
};
static const struct jinvoke_param accounts_accounts_ns_create_params[] = {
    {"owner_uid", "uint32_t"},
    {"kind", "const char *"},
    {"slug", "const char *"},
    {"parent_path", "const char *"}
};
static const struct jinvoke_param accounts_accounts_ns_add_member_params[] = {
    {"path", "const char *"},
    {"uid", "uint32_t"},
    {"role", "const char *"}
};
static const struct jinvoke_param accounts_accounts_ns_resolve_params[] = {
    {"path", "const char *"}
};
static const struct jinvoke_param accounts_accounts_ns_members_params[] = {
    {"path", "const char *"}
};
static const struct jinvoke_param accounts_accounts_ns_remove_member_params[] = {
    {"path", "const char *"},
    {"uid", "uint32_t"}
};
static const struct jinvoke_param accounts_accounts_ns_subtree_params[] = {
    {"path", "const char *"}
};
static const struct jinvoke_param accounts_accounts_ns_delete_params[] = {
    {"path", "const char *"}
};
static const struct jinvoke_param accounts_accounts_list_params[] = {
    {"offset", "int64_t"},
    {"limit", "int64_t"}
};
struct accounts_params_row { const char *name; struct jinvoke_params params; };

static const struct accounts_params_row accounts_params_rows[] = {
    {"accounts_accounts_claim_username", {accounts_accounts_claim_username_params, 2}},
    {"accounts_accounts_release_username", {accounts_accounts_release_username_params, 2}},
    {"accounts_accounts_allocate_uid", {NULL, 0}},
    {"accounts_accounts_uid_for_username", {accounts_accounts_uid_for_username_params, 1}},
    {"accounts_accounts_register", {accounts_accounts_register_params, 2}},
    {"accounts_accounts_exists", {accounts_accounts_exists_params, 1}},
    {"accounts_accounts_set_balance", {accounts_accounts_set_balance_params, 2}},
    {"accounts_accounts_balance", {accounts_accounts_balance_params, 1}},
    {"accounts_accounts_count", {NULL, 0}},
    {"accounts_accounts_set_groups", {accounts_accounts_set_groups_params, 2}},
    {"accounts_accounts_groups", {accounts_accounts_groups_params, 1}},
    {"accounts_accounts_ns_create", {accounts_accounts_ns_create_params, 4}},
    {"accounts_accounts_ns_add_member", {accounts_accounts_ns_add_member_params, 3}},
    {"accounts_accounts_ns_resolve", {accounts_accounts_ns_resolve_params, 1}},
    {"accounts_accounts_ns_list", {NULL, 0}},
    {"accounts_accounts_ns_members", {accounts_accounts_ns_members_params, 1}},
    {"accounts_accounts_ns_remove_member", {accounts_accounts_ns_remove_member_params, 2}},
    {"accounts_accounts_ns_subtree", {accounts_accounts_ns_subtree_params, 1}},
    {"accounts_accounts_ns_delete", {accounts_accounts_ns_delete_params, 1}},
    {"accounts_accounts_list", {accounts_accounts_list_params, 2}},
    {"accounts_accounts_list_all", {NULL, 0}}
};

static const struct jinvoke_params *accounts_params_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(accounts_params_rows) / sizeof(accounts_params_rows[0]); ++i)
        if (strcmp(accounts_params_rows[i].name, qname) == 0)
            return &accounts_params_rows[i].params;
    return NULL;
}
/* ---- accounts: class name → accessor (lazy) ---------------------- */

static struct class_ptr_result accounts_accessor_lookup(const char *name)
{
    if (strcmp(name, "accounts_accounts") == 0) return accounts_accounts_class_get();
    return PICOMESH_OK(class_ptr, NULL);
}

/* ---- accounts: slot → skel, name-keyed static data --------------- */

struct accounts_skel_row { const char *name; rpc_skel_fn fn; };

static const struct accounts_skel_row accounts_skel_rows[] = {
    {"accounts_accounts_claim_username", accounts_accounts_claim_username_skel},
    {"accounts_accounts_release_username", accounts_accounts_release_username_skel},
    {"accounts_accounts_allocate_uid", accounts_accounts_allocate_uid_skel},
    {"accounts_accounts_uid_for_username", accounts_accounts_uid_for_username_skel},
    {"accounts_accounts_register", accounts_accounts_register_skel},
    {"accounts_accounts_exists", accounts_accounts_exists_skel},
    {"accounts_accounts_set_balance", accounts_accounts_set_balance_skel},
    {"accounts_accounts_balance", accounts_accounts_balance_skel},
    {"accounts_accounts_count", accounts_accounts_count_skel},
    {"accounts_accounts_set_groups", accounts_accounts_set_groups_skel},
    {"accounts_accounts_groups", accounts_accounts_groups_skel},
    {"accounts_accounts_ns_create", accounts_accounts_ns_create_skel},
    {"accounts_accounts_ns_add_member", accounts_accounts_ns_add_member_skel},
    {"accounts_accounts_ns_resolve", accounts_accounts_ns_resolve_skel},
    {"accounts_accounts_ns_list", accounts_accounts_ns_list_skel},
    {"accounts_accounts_ns_members", accounts_accounts_ns_members_skel},
    {"accounts_accounts_ns_remove_member", accounts_accounts_ns_remove_member_skel},
    {"accounts_accounts_ns_subtree", accounts_accounts_ns_subtree_skel},
    {"accounts_accounts_ns_delete", accounts_accounts_ns_delete_skel},
    {"accounts_accounts_list", accounts_accounts_list_skel},
    {"accounts_accounts_list_all", accounts_accounts_list_all_skel}
};

static rpc_skel_fn accounts_skel_lookup(const char *name)
{
    /* rpc_skel_for has already resolved the slot to its qname (the only
     * Result-returning step), so this hook is a pure name→fn lookup that
     * never has to swallow an error. */
    for (size_t i = 0; i < sizeof(accounts_skel_rows) / sizeof(accounts_skel_rows[0]); ++i)
        if (strcmp(accounts_skel_rows[i].name, name) == 0)
            return accounts_skel_rows[i].fn;
    return NULL;
}

/* ---- accounts: registration entry point (called from the driver for
 *      config-ACTIVATED plugins only — registration is activation) ---- */

struct picomesh_void_result picomesh_plugin_accounts_register(void)
{
    struct picomesh_void_result _ar = class_add_accessor_lookup(accounts_accessor_lookup);
    PICOMESH_RETURN_IF_ERR(picomesh_void, _ar,
                           "picomesh_plugin_accounts_register: add accessor lookup");
    rpc_add_skel_lookup(accounts_skel_lookup);
    jinvoke_add_lookup(accounts_jinvoke_lookup);
    minvoke_add_lookup(accounts_minvoke_lookup);
    jinvoke_params_add_lookup(accounts_params_lookup);
    { struct class_ptr_result reg = accounts_accounts_class_get();
      PICOMESH_RETURN_IF_ERR(picomesh_void, reg, "accounts register: prewarm accounts_accounts_class_get"); }
    return PICOMESH_OK_VOID();
}
