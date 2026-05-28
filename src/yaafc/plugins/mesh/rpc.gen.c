/* GENERATED — do not edit. */
#include <yaafc/yclass/rpc.h>
#include <yaafc/yclass/jinvoke.h>
#include <yaafc/yjson/yjson.h>
#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/yclass/class.h>
#include "mesh.internal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t mesh_store_register_service_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    size_t _off = 0;
    /* Caller-auth prefix (uid, sid) is the first 8 bytes of every
     * yrpc CALL body — set by the public stub on the way out. */
    struct ctx _local = {0};
    if (_off + 8 > _body_len) goto _short_body;
    memcpy(&_local.uid, (const uint8_t *)_body + _off, 4); _off += 4;
    memcpy(&_local.sid, (const uint8_t *)_body + _off, 4); _off += 4;
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
    struct yaafc_int_result _r = mesh_store_register_service(&_local, _obj, _v1, _v2);
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {
        yaafc_error_print(stderr, "[skel] mesh_store_register_service", _r.error);
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
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return _resp_max >= 1 ? 1 : 0;
}

static size_t mesh_store_resolve_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    size_t _off = 0;
    /* Caller-auth prefix (uid, sid) is the first 8 bytes of every
     * yrpc CALL body — set by the public stub on the way out. */
    struct ctx _local = {0};
    if (_off + 8 > _body_len) goto _short_body;
    memcpy(&_local.uid, (const uint8_t *)_body + _off, 4); _off += 4;
    memcpy(&_local.sid, (const uint8_t *)_body + _off, 4); _off += 4;
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
    struct yaafc_uint32_result _r = mesh_store_resolve(&_local, _obj, _v1);
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {
        yaafc_error_print(stderr, "[skel] mesh_store_resolve", _r.error);
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
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return _resp_max >= 1 ? 1 : 0;
}

static size_t mesh_store_forget_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    size_t _off = 0;
    /* Caller-auth prefix (uid, sid) is the first 8 bytes of every
     * yrpc CALL body — set by the public stub on the way out. */
    struct ctx _local = {0};
    if (_off + 8 > _body_len) goto _short_body;
    memcpy(&_local.uid, (const uint8_t *)_body + _off, 4); _off += 4;
    memcpy(&_local.sid, (const uint8_t *)_body + _off, 4); _off += 4;
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
    struct yaafc_int_result _r = mesh_store_forget(&_local, _obj, _v1);
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {
        yaafc_error_print(stderr, "[skel] mesh_store_forget", _r.error);
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
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return _resp_max >= 1 ? 1 : 0;
}

static size_t mesh_store_count_services_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    size_t _off = 0;
    /* Caller-auth prefix (uid, sid) is the first 8 bytes of every
     * yrpc CALL body — set by the public stub on the way out. */
    struct ctx _local = {0};
    if (_off + 8 > _body_len) goto _short_body;
    memcpy(&_local.uid, (const uint8_t *)_body + _off, 4); _off += 4;
    memcpy(&_local.sid, (const uint8_t *)_body + _off, 4); _off += 4;
    struct object *_obj = NULL;
    {
        if (_off + 8 > _body_len) goto _short_body;
        uint64_t _h;
        memcpy(&_h, (const uint8_t *)_body + _off, 8); _off += 8;
        _obj = (struct object *)rpc_handle_resolve(_h);
    }
    struct yaafc_size_result _r = mesh_store_count_services(&_local, _obj);
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {
        yaafc_error_print(stderr, "[skel] mesh_store_count_services", _r.error);
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
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return _resp_max >= 1 ? 1 : 0;
}

static size_t mesh_store_spawn_yaafc_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    size_t _off = 0;
    /* Caller-auth prefix (uid, sid) is the first 8 bytes of every
     * yrpc CALL body — set by the public stub on the way out. */
    struct ctx _local = {0};
    if (_off + 8 > _body_len) goto _short_body;
    memcpy(&_local.uid, (const uint8_t *)_body + _off, 4); _off += 4;
    memcpy(&_local.sid, (const uint8_t *)_body + _off, 4); _off += 4;
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
    struct yaafc_int_result _r = mesh_store_spawn_yaafc(&_local, _obj, _v1);
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {
        yaafc_error_print(stderr, "[skel] mesh_store_spawn_yaafc", _r.error);
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
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return _resp_max >= 1 ? 1 : 0;
}

static size_t mesh_store_kill_pid_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    size_t _off = 0;
    /* Caller-auth prefix (uid, sid) is the first 8 bytes of every
     * yrpc CALL body — set by the public stub on the way out. */
    struct ctx _local = {0};
    if (_off + 8 > _body_len) goto _short_body;
    memcpy(&_local.uid, (const uint8_t *)_body + _off, 4); _off += 4;
    memcpy(&_local.sid, (const uint8_t *)_body + _off, 4); _off += 4;
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
    struct yaafc_int_result _r = mesh_store_kill_pid(&_local, _obj, _v1);
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {
        yaafc_error_print(stderr, "[skel] mesh_store_kill_pid", _r.error);
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
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return _resp_max >= 1 ? 1 : 0;
}

static size_t mesh_store_count_children_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    size_t _off = 0;
    /* Caller-auth prefix (uid, sid) is the first 8 bytes of every
     * yrpc CALL body — set by the public stub on the way out. */
    struct ctx _local = {0};
    if (_off + 8 > _body_len) goto _short_body;
    memcpy(&_local.uid, (const uint8_t *)_body + _off, 4); _off += 4;
    memcpy(&_local.sid, (const uint8_t *)_body + _off, 4); _off += 4;
    struct object *_obj = NULL;
    {
        if (_off + 8 > _body_len) goto _short_body;
        uint64_t _h;
        memcpy(&_h, (const uint8_t *)_body + _off, 8); _off += 8;
        _obj = (struct object *)rpc_handle_resolve(_h);
    }
    struct yaafc_size_result _r = mesh_store_count_children(&_local, _obj);
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {
        yaafc_error_print(stderr, "[skel] mesh_store_count_children", _r.error);
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
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return _resp_max >= 1 ? 1 : 0;
}

static size_t mesh_store_reconcile_from_config_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    size_t _off = 0;
    /* Caller-auth prefix (uid, sid) is the first 8 bytes of every
     * yrpc CALL body — set by the public stub on the way out. */
    struct ctx _local = {0};
    if (_off + 8 > _body_len) goto _short_body;
    memcpy(&_local.uid, (const uint8_t *)_body + _off, 4); _off += 4;
    memcpy(&_local.sid, (const uint8_t *)_body + _off, 4); _off += 4;
    struct object *_obj = NULL;
    {
        if (_off + 8 > _body_len) goto _short_body;
        uint64_t _h;
        memcpy(&_h, (const uint8_t *)_body + _off, 8); _off += 8;
        _obj = (struct object *)rpc_handle_resolve(_h);
    }
    struct yaafc_int_result _r = mesh_store_reconcile_from_config(&_local, _obj);
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {
        yaafc_error_print(stderr, "[skel] mesh_store_reconcile_from_config", _r.error);
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
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return _resp_max >= 1 ? 1 : 0;
}

static size_t mesh_store_reconcile_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    size_t _off = 0;
    /* Caller-auth prefix (uid, sid) is the first 8 bytes of every
     * yrpc CALL body — set by the public stub on the way out. */
    struct ctx _local = {0};
    if (_off + 8 > _body_len) goto _short_body;
    memcpy(&_local.uid, (const uint8_t *)_body + _off, 4); _off += 4;
    memcpy(&_local.sid, (const uint8_t *)_body + _off, 4); _off += 4;
    struct object *_obj = NULL;
    {
        if (_off + 8 > _body_len) goto _short_body;
        uint64_t _h;
        memcpy(&_h, (const uint8_t *)_body + _off, 8); _off += 8;
        _obj = (struct object *)rpc_handle_resolve(_h);
    }
    struct yaafc_int_result _r = mesh_store_reconcile(&_local, _obj);
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {
        yaafc_error_print(stderr, "[skel] mesh_store_reconcile", _r.error);
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
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return _resp_max >= 1 ? 1 : 0;
}

static int mesh_store_register_service_jinvoke(struct object *_obj, const struct yjson_value *_args,
                          struct yjson_writer *_result, char *_err, size_t _err_cap)
{
    uint32_t _a0 = (uint32_t)yjson_as_int(yjson_array_at(_args, 0), 0);
    uint32_t _a1 = (uint32_t)yjson_as_int(yjson_array_at(_args, 1), 0);
    struct ctx _ctx = {0};
    struct yaafc_int_result _r = mesh_store_register_service(&_ctx, _obj, _a0, _a1);
    if (YAAFC_IS_ERR(_r)) {
        snprintf(_err, _err_cap, "%s: %s", "mesh_store_register_service",
                 _r.error.msg ? _r.error.msg : "<no message>");
        yaafc_error_destroy(_r.error);
        return -1;
    }
    yjson_w_int(_result, (int64_t)_r.value);
    return 0;
}

static int mesh_store_resolve_jinvoke(struct object *_obj, const struct yjson_value *_args,
                          struct yjson_writer *_result, char *_err, size_t _err_cap)
{
    uint32_t _a0 = (uint32_t)yjson_as_int(yjson_array_at(_args, 0), 0);
    struct ctx _ctx = {0};
    struct yaafc_uint32_result _r = mesh_store_resolve(&_ctx, _obj, _a0);
    if (YAAFC_IS_ERR(_r)) {
        snprintf(_err, _err_cap, "%s: %s", "mesh_store_resolve",
                 _r.error.msg ? _r.error.msg : "<no message>");
        yaafc_error_destroy(_r.error);
        return -1;
    }
    yjson_w_int(_result, (int64_t)_r.value);
    return 0;
}

static int mesh_store_forget_jinvoke(struct object *_obj, const struct yjson_value *_args,
                          struct yjson_writer *_result, char *_err, size_t _err_cap)
{
    uint32_t _a0 = (uint32_t)yjson_as_int(yjson_array_at(_args, 0), 0);
    struct ctx _ctx = {0};
    struct yaafc_int_result _r = mesh_store_forget(&_ctx, _obj, _a0);
    if (YAAFC_IS_ERR(_r)) {
        snprintf(_err, _err_cap, "%s: %s", "mesh_store_forget",
                 _r.error.msg ? _r.error.msg : "<no message>");
        yaafc_error_destroy(_r.error);
        return -1;
    }
    yjson_w_int(_result, (int64_t)_r.value);
    return 0;
}

static int mesh_store_count_services_jinvoke(struct object *_obj, const struct yjson_value *_args,
                          struct yjson_writer *_result, char *_err, size_t _err_cap)
{
    struct ctx _ctx = {0};
    struct yaafc_size_result _r = mesh_store_count_services(&_ctx, _obj);
    if (YAAFC_IS_ERR(_r)) {
        snprintf(_err, _err_cap, "%s: %s", "mesh_store_count_services",
                 _r.error.msg ? _r.error.msg : "<no message>");
        yaafc_error_destroy(_r.error);
        return -1;
    }
    yjson_w_int(_result, (int64_t)_r.value);
    return 0;
}

static int mesh_store_spawn_yaafc_jinvoke(struct object *_obj, const struct yjson_value *_args,
                          struct yjson_writer *_result, char *_err, size_t _err_cap)
{
    uint32_t _a0 = (uint32_t)yjson_as_int(yjson_array_at(_args, 0), 0);
    struct ctx _ctx = {0};
    struct yaafc_int_result _r = mesh_store_spawn_yaafc(&_ctx, _obj, _a0);
    if (YAAFC_IS_ERR(_r)) {
        snprintf(_err, _err_cap, "%s: %s", "mesh_store_spawn_yaafc",
                 _r.error.msg ? _r.error.msg : "<no message>");
        yaafc_error_destroy(_r.error);
        return -1;
    }
    yjson_w_int(_result, (int64_t)_r.value);
    return 0;
}

static int mesh_store_kill_pid_jinvoke(struct object *_obj, const struct yjson_value *_args,
                          struct yjson_writer *_result, char *_err, size_t _err_cap)
{
    int32_t _a0 = (int32_t)yjson_as_int(yjson_array_at(_args, 0), 0);
    struct ctx _ctx = {0};
    struct yaafc_int_result _r = mesh_store_kill_pid(&_ctx, _obj, _a0);
    if (YAAFC_IS_ERR(_r)) {
        snprintf(_err, _err_cap, "%s: %s", "mesh_store_kill_pid",
                 _r.error.msg ? _r.error.msg : "<no message>");
        yaafc_error_destroy(_r.error);
        return -1;
    }
    yjson_w_int(_result, (int64_t)_r.value);
    return 0;
}

static int mesh_store_count_children_jinvoke(struct object *_obj, const struct yjson_value *_args,
                          struct yjson_writer *_result, char *_err, size_t _err_cap)
{
    struct ctx _ctx = {0};
    struct yaafc_size_result _r = mesh_store_count_children(&_ctx, _obj);
    if (YAAFC_IS_ERR(_r)) {
        snprintf(_err, _err_cap, "%s: %s", "mesh_store_count_children",
                 _r.error.msg ? _r.error.msg : "<no message>");
        yaafc_error_destroy(_r.error);
        return -1;
    }
    yjson_w_int(_result, (int64_t)_r.value);
    return 0;
}

static int mesh_store_reconcile_from_config_jinvoke(struct object *_obj, const struct yjson_value *_args,
                          struct yjson_writer *_result, char *_err, size_t _err_cap)
{
    struct ctx _ctx = {0};
    struct yaafc_int_result _r = mesh_store_reconcile_from_config(&_ctx, _obj);
    if (YAAFC_IS_ERR(_r)) {
        snprintf(_err, _err_cap, "%s: %s", "mesh_store_reconcile_from_config",
                 _r.error.msg ? _r.error.msg : "<no message>");
        yaafc_error_destroy(_r.error);
        return -1;
    }
    yjson_w_int(_result, (int64_t)_r.value);
    return 0;
}

static int mesh_store_reconcile_jinvoke(struct object *_obj, const struct yjson_value *_args,
                          struct yjson_writer *_result, char *_err, size_t _err_cap)
{
    struct ctx _ctx = {0};
    struct yaafc_int_result _r = mesh_store_reconcile(&_ctx, _obj);
    if (YAAFC_IS_ERR(_r)) {
        snprintf(_err, _err_cap, "%s: %s", "mesh_store_reconcile",
                 _r.error.msg ? _r.error.msg : "<no message>");
        yaafc_error_destroy(_r.error);
        return -1;
    }
    yjson_w_int(_result, (int64_t)_r.value);
    return 0;
}

struct object_ptr_result mesh_store_create(struct ctx *ctx)
{
    ydebug("class=mesh_store");
    struct class_ptr_result _kr = mesh_store_class_get();
    if (YAAFC_IS_ERR(_kr))
        return YAAFC_ERR(object_ptr, "mesh_store_create: class accessor failed", _kr);
    const struct class *_klass = _kr.value;

    if (!ctx || !ctx->session)
        return object_alloc(_klass);

    rpc_session_translate_class(ctx->session, "mesh_store");

    uint64_t _h = 0;
    const char *_name = "mesh_store";
    if (rpc_call(ctx->session, RPC_OP_CREATE, 0, _name, strlen(_name),
                 &_h, sizeof(_h)) != sizeof(_h) || !_h)
        return YAAFC_ERR(object_ptr, "mesh_store_create: remote create failed");

    void *_mem = calloc(1, sizeof(struct object) + sizeof(uint64_t));
    if (!_mem)
        return YAAFC_ERR(object_ptr, "mesh_store_create: calloc(proxy) failed");
    struct object *_obj = _mem;
    *(const struct class **)_obj = _klass;
    *(uint64_t *)((char *)_obj + sizeof(*_obj)) = _h;
    return YAAFC_OK(object_ptr, _obj);
}


/* ---- mesh: jinvoke table ------------------------------------ */

struct mesh_jinvoke_row { const char *name; jinvoke_fn fn; };

static const struct mesh_jinvoke_row mesh_jinvoke_rows[] = {
    {"mesh_store_register_service", mesh_store_register_service_jinvoke},
    {"mesh_store_resolve", mesh_store_resolve_jinvoke},
    {"mesh_store_forget", mesh_store_forget_jinvoke},
    {"mesh_store_count_services", mesh_store_count_services_jinvoke},
    {"mesh_store_spawn_yaafc", mesh_store_spawn_yaafc_jinvoke},
    {"mesh_store_kill_pid", mesh_store_kill_pid_jinvoke},
    {"mesh_store_count_children", mesh_store_count_children_jinvoke},
    {"mesh_store_reconcile_from_config", mesh_store_reconcile_from_config_jinvoke},
    {"mesh_store_reconcile", mesh_store_reconcile_jinvoke}
};

static jinvoke_fn mesh_jinvoke_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(mesh_jinvoke_rows) / sizeof(mesh_jinvoke_rows[0]); ++i)
        if (strcmp(mesh_jinvoke_rows[i].name, qname) == 0)
            return mesh_jinvoke_rows[i].fn;
    return NULL;
}
/* ---- mesh: class name → accessor (lazy) ---------------------- */

static struct class_ptr_result mesh_accessor_lookup(const char *name)
{
    if (strcmp(name, "mesh_store") == 0) return mesh_store_class_get();
    return YAAFC_OK(class_ptr, NULL);
}

/* ---- mesh: slot → skel, name-keyed static data --------------- */

struct mesh_skel_row { const char *name; rpc_skel_fn fn; };

static const struct mesh_skel_row mesh_skel_rows[] = {
    {"mesh_store_register_service", mesh_store_register_service_skel},
    {"mesh_store_resolve", mesh_store_resolve_skel},
    {"mesh_store_forget", mesh_store_forget_skel},
    {"mesh_store_count_services", mesh_store_count_services_skel},
    {"mesh_store_spawn_yaafc", mesh_store_spawn_yaafc_skel},
    {"mesh_store_kill_pid", mesh_store_kill_pid_skel},
    {"mesh_store_count_children", mesh_store_count_children_skel},
    {"mesh_store_reconcile_from_config", mesh_store_reconcile_from_config_skel},
    {"mesh_store_reconcile", mesh_store_reconcile_skel}
};

static rpc_skel_fn mesh_skel_lookup(method_slot slot)
{
    struct const_char_ptr_result nr = method_slot_name(slot);
    if (YAAFC_IS_ERR(nr)) { yaafc_error_destroy(nr.error); return NULL; }
    const char *name = nr.value;
    for (size_t i = 0; i < sizeof(mesh_skel_rows) / sizeof(mesh_skel_rows[0]); ++i)
        if (strcmp(mesh_skel_rows[i].name, name) == 0)
            return mesh_skel_rows[i].fn;
    return NULL;
}

/* ---- mesh: install hooks before main ------------------------- */

__attribute__((constructor))
static void mesh_install_hooks(void)
{
    struct yaafc_void_result _ar = class_add_accessor_lookup(mesh_accessor_lookup);
    if (YAAFC_IS_ERR(_ar)) {
        yaafc_error_print(stderr, "mesh_install_hooks", _ar.error);
        yaafc_error_destroy(_ar.error);
        abort();
    }
    rpc_add_skel_lookup(mesh_skel_lookup);
    jinvoke_add_lookup(mesh_jinvoke_lookup);
}
