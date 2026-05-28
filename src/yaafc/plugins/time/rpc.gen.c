/* GENERATED — do not edit. */
#include <yaafc/yclass/rpc.h>
#include <yaafc/yclass/jinvoke.h>
#include <yaafc/yjson/yjson.h>
#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/yclass/class.h>
#include "time.internal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t time_clock_now_ms_skel(const void *_body, size_t _body_len,
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
    struct yaafc_int64_result _r = time_clock_now_ms(&_local, _obj);
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {
        yaafc_error_print(stderr, "[skel] time_clock_now_ms", _r.error);
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

static size_t time_clock_sleep_ms_skel(const void *_body, size_t _body_len,
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
    struct yaafc_int64_result _r = time_clock_sleep_ms(&_local, _obj, _v1);
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {
        yaafc_error_print(stderr, "[skel] time_clock_sleep_ms", _r.error);
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

static int time_clock_now_ms_jinvoke(struct object *_obj, const struct yjson_value *_args,
                          struct yjson_writer *_result, char *_err, size_t _err_cap)
{
    struct ctx _ctx = {0};
    struct yaafc_int64_result _r = time_clock_now_ms(&_ctx, _obj);
    if (YAAFC_IS_ERR(_r)) {
        snprintf(_err, _err_cap, "%s: %s", "time_clock_now_ms",
                 _r.error.msg ? _r.error.msg : "<no message>");
        yaafc_error_destroy(_r.error);
        return -1;
    }
    yjson_w_int(_result, (int64_t)_r.value);
    return 0;
}

static int time_clock_sleep_ms_jinvoke(struct object *_obj, const struct yjson_value *_args,
                          struct yjson_writer *_result, char *_err, size_t _err_cap)
{
    uint32_t _a0 = (uint32_t)yjson_as_int(yjson_array_at(_args, 0), 0);
    struct ctx _ctx = {0};
    struct yaafc_int64_result _r = time_clock_sleep_ms(&_ctx, _obj, _a0);
    if (YAAFC_IS_ERR(_r)) {
        snprintf(_err, _err_cap, "%s: %s", "time_clock_sleep_ms",
                 _r.error.msg ? _r.error.msg : "<no message>");
        yaafc_error_destroy(_r.error);
        return -1;
    }
    yjson_w_int(_result, (int64_t)_r.value);
    return 0;
}

struct object_ptr_result time_clock_create(struct ctx *ctx)
{
    ydebug("class=time_clock");
    struct class_ptr_result _kr = time_clock_class_get();
    if (YAAFC_IS_ERR(_kr))
        return YAAFC_ERR(object_ptr, "time_clock_create: class accessor failed", _kr);
    const struct class *_klass = _kr.value;

    if (!ctx || !ctx->session)
        return object_alloc(_klass);

    rpc_session_translate_class(ctx->session, "time_clock");

    uint64_t _h = 0;
    const char *_name = "time_clock";
    if (rpc_call(ctx->session, RPC_OP_CREATE, 0, _name, strlen(_name),
                 &_h, sizeof(_h)) != sizeof(_h) || !_h)
        return YAAFC_ERR(object_ptr, "time_clock_create: remote create failed");

    void *_mem = calloc(1, sizeof(struct object) + sizeof(uint64_t));
    if (!_mem)
        return YAAFC_ERR(object_ptr, "time_clock_create: calloc(proxy) failed");
    struct object *_obj = _mem;
    *(const struct class **)_obj = _klass;
    *(uint64_t *)((char *)_obj + sizeof(*_obj)) = _h;
    return YAAFC_OK(object_ptr, _obj);
}


/* ---- time: jinvoke table ------------------------------------ */

struct time_jinvoke_row { const char *name; jinvoke_fn fn; };

static const struct time_jinvoke_row time_jinvoke_rows[] = {
    {"time_clock_now_ms", time_clock_now_ms_jinvoke},
    {"time_clock_sleep_ms", time_clock_sleep_ms_jinvoke}
};

static jinvoke_fn time_jinvoke_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(time_jinvoke_rows) / sizeof(time_jinvoke_rows[0]); ++i)
        if (strcmp(time_jinvoke_rows[i].name, qname) == 0)
            return time_jinvoke_rows[i].fn;
    return NULL;
}
/* ---- time: class name → accessor (lazy) ---------------------- */

static struct class_ptr_result time_accessor_lookup(const char *name)
{
    if (strcmp(name, "time_clock") == 0) return time_clock_class_get();
    return YAAFC_OK(class_ptr, NULL);
}

/* ---- time: slot → skel, name-keyed static data --------------- */

struct time_skel_row { const char *name; rpc_skel_fn fn; };

static const struct time_skel_row time_skel_rows[] = {
    {"time_clock_now_ms", time_clock_now_ms_skel},
    {"time_clock_sleep_ms", time_clock_sleep_ms_skel}
};

static rpc_skel_fn time_skel_lookup(method_slot slot)
{
    struct const_char_ptr_result nr = method_slot_name(slot);
    if (YAAFC_IS_ERR(nr)) { yaafc_error_destroy(nr.error); return NULL; }
    const char *name = nr.value;
    for (size_t i = 0; i < sizeof(time_skel_rows) / sizeof(time_skel_rows[0]); ++i)
        if (strcmp(time_skel_rows[i].name, name) == 0)
            return time_skel_rows[i].fn;
    return NULL;
}

/* ---- time: install hooks before main ------------------------- */

__attribute__((constructor))
static void time_install_hooks(void)
{
    struct yaafc_void_result _ar = class_add_accessor_lookup(time_accessor_lookup);
    if (YAAFC_IS_ERR(_ar)) {
        yaafc_error_print(stderr, "time_install_hooks", _ar.error);
        yaafc_error_destroy(_ar.error);
        abort();
    }
    rpc_add_skel_lookup(time_skel_lookup);
    jinvoke_add_lookup(time_jinvoke_lookup);
}
