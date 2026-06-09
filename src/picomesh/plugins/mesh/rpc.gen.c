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
#include "mesh.internal.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct picomesh_size_result mesh_mesh_register_service_skel(const void *_body, size_t _body_len,
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
    uint32_t _v2 = 0;
    if (_off + sizeof(_v2) > _body_len) goto _short_body;
    memcpy(&_v2, (const uint8_t *)_body + _off, sizeof(_v2));
    _off += sizeof(_v2);
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.mesh_mesh_register_service");
    struct picomesh_int_result _r = mesh_mesh_register_service(&_local, _obj, _hdrs, _v1, _v2);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "mesh_mesh_register_service_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "mesh_mesh_register_service_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result mesh_mesh_resolve_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.mesh_mesh_resolve");
    struct picomesh_uint32_result _r = mesh_mesh_resolve(&_local, _obj, _hdrs, _v1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "mesh_mesh_resolve_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "mesh_mesh_resolve_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result mesh_mesh_forget_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.mesh_mesh_forget");
    struct picomesh_int_result _r = mesh_mesh_forget(&_local, _obj, _hdrs, _v1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "mesh_mesh_forget_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "mesh_mesh_forget_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result mesh_mesh_count_services_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.mesh_mesh_count_services");
    struct picomesh_size_result _r = mesh_mesh_count_services(&_local, _obj, _hdrs);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "mesh_mesh_count_services_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "mesh_mesh_count_services_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result mesh_mesh_spawn_picomesh_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.mesh_mesh_spawn_picomesh");
    struct picomesh_int_result _r = mesh_mesh_spawn_picomesh(&_local, _obj, _hdrs, _v1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "mesh_mesh_spawn_picomesh_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "mesh_mesh_spawn_picomesh_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result mesh_mesh_kill_pid_skel(const void *_body, size_t _body_len,
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
    int32_t _v1 = 0;
    if (_off + sizeof(_v1) > _body_len) goto _short_body;
    memcpy(&_v1, (const uint8_t *)_body + _off, sizeof(_v1));
    _off += sizeof(_v1);
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.mesh_mesh_kill_pid");
    struct picomesh_int_result _r = mesh_mesh_kill_pid(&_local, _obj, _hdrs, _v1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "mesh_mesh_kill_pid_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "mesh_mesh_kill_pid_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result mesh_mesh_count_children_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.mesh_mesh_count_children");
    struct picomesh_size_result _r = mesh_mesh_count_children(&_local, _obj, _hdrs);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "mesh_mesh_count_children_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "mesh_mesh_count_children_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result mesh_mesh_reconcile_from_config_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.mesh_mesh_reconcile_from_config");
    struct picomesh_int_result _r = mesh_mesh_reconcile_from_config(&_local, _obj, _hdrs);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "mesh_mesh_reconcile_from_config_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "mesh_mesh_reconcile_from_config_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result mesh_mesh_reconcile_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.mesh_mesh_reconcile");
    struct picomesh_int_result _r = mesh_mesh_reconcile(&_local, _obj, _hdrs);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "mesh_mesh_reconcile_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "mesh_mesh_reconcile_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_void_result mesh_mesh_register_service_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] mesh_mesh_register_service");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    uint32_t arg1 = (uint32_t)json_as_int(json_array_at(args, 1), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = mesh_mesh_register_service(call_ctx, obj, hdrs, arg0, arg1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "mesh_mesh_register_service",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_register_service", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result mesh_mesh_resolve_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] mesh_mesh_resolve");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_uint32_result call_result = mesh_mesh_resolve(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "mesh_mesh_resolve",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_resolve", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result mesh_mesh_forget_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] mesh_mesh_forget");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = mesh_mesh_forget(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "mesh_mesh_forget",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_forget", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result mesh_mesh_count_services_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] mesh_mesh_count_services");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = mesh_mesh_count_services(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "mesh_mesh_count_services",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_count_services", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result mesh_mesh_spawn_picomesh_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] mesh_mesh_spawn_picomesh");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = mesh_mesh_spawn_picomesh(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "mesh_mesh_spawn_picomesh",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_spawn_picomesh", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result mesh_mesh_kill_pid_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] mesh_mesh_kill_pid");
    int32_t arg0 = (int32_t)json_as_int(json_array_at(args, 0), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = mesh_mesh_kill_pid(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "mesh_mesh_kill_pid",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_kill_pid", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result mesh_mesh_count_children_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] mesh_mesh_count_children");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = mesh_mesh_count_children(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "mesh_mesh_count_children",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_count_children", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result mesh_mesh_reconcile_from_config_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] mesh_mesh_reconcile_from_config");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = mesh_mesh_reconcile_from_config(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "mesh_mesh_reconcile_from_config",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_reconcile_from_config", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result mesh_mesh_reconcile_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] mesh_mesh_reconcile");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = mesh_mesh_reconcile(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "mesh_mesh_reconcile",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_reconcile", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result mesh_mesh_register_service_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 2u) {
        snprintf(_err, _err_cap, "mesh_mesh_register_service: expected 2 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_register_service: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "service_id: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "service_id: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    uint32_t _v1;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "port: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "port: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v1 = (uint32_t)_u;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = mesh_mesh_register_service(call_ctx, obj, hdrs, _v0, _v1);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "mesh_mesh_register_service",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_register_service", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result mesh_mesh_resolve_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "mesh_mesh_resolve: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_resolve: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "service_id: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "service_id: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_uint32_result call_result = mesh_mesh_resolve(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "mesh_mesh_resolve",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_resolve", call_result);
    }
    cmp_write_uinteger(_mw, (uint64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result mesh_mesh_forget_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "mesh_mesh_forget: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_forget: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "service_id: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "service_id: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = mesh_mesh_forget(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "mesh_mesh_forget",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_forget", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result mesh_mesh_count_services_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 0u) {
        snprintf(_err, _err_cap, "mesh_mesh_count_services: expected 0 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_count_services: wrong argument count");
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = mesh_mesh_count_services(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "mesh_mesh_count_services",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_count_services", call_result);
    }
    cmp_write_uinteger(_mw, (uint64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result mesh_mesh_spawn_picomesh_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "mesh_mesh_spawn_picomesh: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_spawn_picomesh: wrong argument count");
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
    struct picomesh_int_result call_result = mesh_mesh_spawn_picomesh(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "mesh_mesh_spawn_picomesh",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_spawn_picomesh", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result mesh_mesh_kill_pid_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "mesh_mesh_kill_pid: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_kill_pid: wrong argument count");
    }
    int32_t _v0;
    {
        int64_t _i;
        if (!cmp_read_integer(_mr, &_i)) { snprintf(_err, _err_cap, "pid: expected int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_i < (INT32_MIN) || _i > (INT32_MAX)) { snprintf(_err, _err_cap, "pid: value %lld out of range for int32_t", (long long)_i); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (int32_t)_i;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = mesh_mesh_kill_pid(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "mesh_mesh_kill_pid",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_kill_pid", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result mesh_mesh_count_children_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 0u) {
        snprintf(_err, _err_cap, "mesh_mesh_count_children: expected 0 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_count_children: wrong argument count");
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_size_result call_result = mesh_mesh_count_children(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "mesh_mesh_count_children",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_count_children", call_result);
    }
    cmp_write_uinteger(_mw, (uint64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result mesh_mesh_reconcile_from_config_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 0u) {
        snprintf(_err, _err_cap, "mesh_mesh_reconcile_from_config: expected 0 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_reconcile_from_config: wrong argument count");
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = mesh_mesh_reconcile_from_config(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "mesh_mesh_reconcile_from_config",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_reconcile_from_config", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result mesh_mesh_reconcile_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 0u) {
        snprintf(_err, _err_cap, "mesh_mesh_reconcile: expected 0 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_reconcile: wrong argument count");
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int_result call_result = mesh_mesh_reconcile(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "mesh_mesh_reconcile",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "mesh_mesh_reconcile", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

struct object_ptr_result mesh_mesh_create(struct ctx *ctx)
{
    ydebug("class=mesh_mesh");
    struct class_ptr_result _kr = mesh_mesh_class_get();
    if (PICOMESH_IS_ERR(_kr))
        return PICOMESH_ERR(object_ptr, "mesh_mesh_create: class accessor failed", _kr);
    /* A service dependency is acquired once and cached for the connection
     * (remote) / process (in-process) lifetime — no per-call create. */
    return rpc_object_acquire(ctx, _kr.value, "mesh_mesh");
}


/* ---- mesh: jinvoke table ------------------------------------ */

struct mesh_jinvoke_row { const char *name; jinvoke_fn fn; };

static const struct mesh_jinvoke_row mesh_jinvoke_rows[] = {
    {"mesh_mesh_register_service", mesh_mesh_register_service_jinvoke},
    {"mesh_mesh_resolve", mesh_mesh_resolve_jinvoke},
    {"mesh_mesh_forget", mesh_mesh_forget_jinvoke},
    {"mesh_mesh_count_services", mesh_mesh_count_services_jinvoke},
    {"mesh_mesh_spawn_picomesh", mesh_mesh_spawn_picomesh_jinvoke},
    {"mesh_mesh_kill_pid", mesh_mesh_kill_pid_jinvoke},
    {"mesh_mesh_count_children", mesh_mesh_count_children_jinvoke},
    {"mesh_mesh_reconcile_from_config", mesh_mesh_reconcile_from_config_jinvoke},
    {"mesh_mesh_reconcile", mesh_mesh_reconcile_jinvoke}
};

static jinvoke_fn mesh_jinvoke_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(mesh_jinvoke_rows) / sizeof(mesh_jinvoke_rows[0]); ++i)
        if (strcmp(mesh_jinvoke_rows[i].name, qname) == 0)
            return mesh_jinvoke_rows[i].fn;
    return NULL;
}

/* ---- mesh: minvoke table ------------------------------------ */

struct mesh_minvoke_row { const char *name; minvoke_fn fn; };

static const struct mesh_minvoke_row mesh_minvoke_rows[] = {
    {"mesh_mesh_register_service", mesh_mesh_register_service_minvoke},
    {"mesh_mesh_resolve", mesh_mesh_resolve_minvoke},
    {"mesh_mesh_forget", mesh_mesh_forget_minvoke},
    {"mesh_mesh_count_services", mesh_mesh_count_services_minvoke},
    {"mesh_mesh_spawn_picomesh", mesh_mesh_spawn_picomesh_minvoke},
    {"mesh_mesh_kill_pid", mesh_mesh_kill_pid_minvoke},
    {"mesh_mesh_count_children", mesh_mesh_count_children_minvoke},
    {"mesh_mesh_reconcile_from_config", mesh_mesh_reconcile_from_config_minvoke},
    {"mesh_mesh_reconcile", mesh_mesh_reconcile_minvoke}
};

static minvoke_fn mesh_minvoke_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(mesh_minvoke_rows) / sizeof(mesh_minvoke_rows[0]); ++i)
        if (strcmp(mesh_minvoke_rows[i].name, qname) == 0)
            return mesh_minvoke_rows[i].fn;
    return NULL;
}

/* ---- mesh: per-method parameter signatures (runtime reflection) -- */

static const struct jinvoke_param mesh_mesh_register_service_params[] = {
    {"service_id", "uint32_t"},
    {"port", "uint32_t"}
};
static const struct jinvoke_param mesh_mesh_resolve_params[] = {
    {"service_id", "uint32_t"}
};
static const struct jinvoke_param mesh_mesh_forget_params[] = {
    {"service_id", "uint32_t"}
};
static const struct jinvoke_param mesh_mesh_spawn_picomesh_params[] = {
    {"port", "uint32_t"}
};
static const struct jinvoke_param mesh_mesh_kill_pid_params[] = {
    {"pid", "int32_t"}
};
struct mesh_params_row { const char *name; struct jinvoke_params params; };

static const struct mesh_params_row mesh_params_rows[] = {
    {"mesh_mesh_register_service", {mesh_mesh_register_service_params, 2}},
    {"mesh_mesh_resolve", {mesh_mesh_resolve_params, 1}},
    {"mesh_mesh_forget", {mesh_mesh_forget_params, 1}},
    {"mesh_mesh_count_services", {NULL, 0}},
    {"mesh_mesh_spawn_picomesh", {mesh_mesh_spawn_picomesh_params, 1}},
    {"mesh_mesh_kill_pid", {mesh_mesh_kill_pid_params, 1}},
    {"mesh_mesh_count_children", {NULL, 0}},
    {"mesh_mesh_reconcile_from_config", {NULL, 0}},
    {"mesh_mesh_reconcile", {NULL, 0}}
};

static const struct jinvoke_params *mesh_params_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(mesh_params_rows) / sizeof(mesh_params_rows[0]); ++i)
        if (strcmp(mesh_params_rows[i].name, qname) == 0)
            return &mesh_params_rows[i].params;
    return NULL;
}
/* ---- mesh: class name → accessor (lazy) ---------------------- */

static struct class_ptr_result mesh_accessor_lookup(const char *name)
{
    if (strcmp(name, "mesh_mesh") == 0) return mesh_mesh_class_get();
    return PICOMESH_OK(class_ptr, NULL);
}

/* ---- mesh: slot → skel, name-keyed static data --------------- */

struct mesh_skel_row { const char *name; rpc_skel_fn fn; };

static const struct mesh_skel_row mesh_skel_rows[] = {
    {"mesh_mesh_register_service", mesh_mesh_register_service_skel},
    {"mesh_mesh_resolve", mesh_mesh_resolve_skel},
    {"mesh_mesh_forget", mesh_mesh_forget_skel},
    {"mesh_mesh_count_services", mesh_mesh_count_services_skel},
    {"mesh_mesh_spawn_picomesh", mesh_mesh_spawn_picomesh_skel},
    {"mesh_mesh_kill_pid", mesh_mesh_kill_pid_skel},
    {"mesh_mesh_count_children", mesh_mesh_count_children_skel},
    {"mesh_mesh_reconcile_from_config", mesh_mesh_reconcile_from_config_skel},
    {"mesh_mesh_reconcile", mesh_mesh_reconcile_skel}
};

static rpc_skel_fn mesh_skel_lookup(const char *name)
{
    /* rpc_skel_for has already resolved the slot to its qname (the only
     * Result-returning step), so this hook is a pure name→fn lookup that
     * never has to swallow an error. */
    for (size_t i = 0; i < sizeof(mesh_skel_rows) / sizeof(mesh_skel_rows[0]); ++i)
        if (strcmp(mesh_skel_rows[i].name, name) == 0)
            return mesh_skel_rows[i].fn;
    return NULL;
}

/* ---- mesh: registration entry point (called from the driver for
 *      config-ACTIVATED plugins only — registration is activation) ---- */

struct picomesh_void_result picomesh_plugin_mesh_register(void)
{
    struct picomesh_void_result _ar = class_add_accessor_lookup(mesh_accessor_lookup);
    PICOMESH_RETURN_IF_ERR(picomesh_void, _ar,
                           "picomesh_plugin_mesh_register: add accessor lookup");
    rpc_add_skel_lookup(mesh_skel_lookup);
    jinvoke_add_lookup(mesh_jinvoke_lookup);
    minvoke_add_lookup(mesh_minvoke_lookup);
    jinvoke_params_add_lookup(mesh_params_lookup);
    { struct class_ptr_result reg = mesh_mesh_class_get();
      PICOMESH_RETURN_IF_ERR(picomesh_void, reg, "mesh register: prewarm mesh_mesh_class_get"); }
    return PICOMESH_OK_VOID();
}
