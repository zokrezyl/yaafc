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
#include "time.internal.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct picomesh_size_result time_clock_now_ms_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.time_clock_now_ms");
    struct picomesh_int64_result _r = time_clock_now_ms(&_local, _obj, _hdrs);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "time_clock_now_ms_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "time_clock_now_ms_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_size_result time_clock_sleep_ms_skel(const void *_body, size_t _body_len,
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
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.time_clock_sleep_ms");
    struct picomesh_int64_result _r = time_clock_sleep_ms(&_local, _obj, _hdrs, _v1);
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "time_clock_sleep_ms_skel: response buffer too small");
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
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "time_clock_sleep_ms_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
}

static struct picomesh_void_result time_clock_now_ms_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] time_clock_now_ms");
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = time_clock_now_ms(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "time_clock_now_ms",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "time_clock_now_ms", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result time_clock_sleep_ms_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{
    yinfo("[rpc] time_clock_sleep_ms");
    uint32_t arg0 = (uint32_t)json_as_int(json_array_at(args, 0), 0);
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = time_clock_sleep_ms(call_ctx, obj, hdrs, arg0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "time_clock_sleep_ms",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "time_clock_sleep_ms", call_result);
    }
    json_writer_int(result, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result time_clock_now_ms_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 0u) {
        snprintf(_err, _err_cap, "time_clock_now_ms: expected 0 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "time_clock_now_ms: wrong argument count");
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = time_clock_now_ms(call_ctx, obj, hdrs);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "time_clock_now_ms",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "time_clock_now_ms", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result time_clock_sleep_ms_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{
    (void)_mr;
    if (_argc != 1u) {
        snprintf(_err, _err_cap, "time_clock_sleep_ms: expected 1 arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "time_clock_sleep_ms: wrong argument count");
    }
    uint32_t _v0;
    {
        uint64_t _u;
        if (!cmp_read_uinteger(_mr, &_u)) { snprintf(_err, _err_cap, "ms: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        if (_u > UINT32_MAX) { snprintf(_err, _err_cap, "ms: value %llu out of range for uint32_t", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }
        _v0 = (uint32_t)_u;
    }
    struct ctx local_ctx = {0};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    struct picomesh_int64_result call_result = time_clock_sleep_ms(call_ctx, obj, hdrs, _v0);
    if (PICOMESH_IS_ERR(call_result)) {
        char chain[8192] = {0};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "time_clock_sleep_ms",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "time_clock_sleep_ms", call_result);
    }
    cmp_write_integer(_mw, (int64_t)call_result.value);
    return PICOMESH_OK_VOID();
}

struct object_ptr_result time_clock_create(struct ctx *ctx)
{
    ydebug("class=time_clock");
    struct class_ptr_result _kr = time_clock_class_get();
    if (PICOMESH_IS_ERR(_kr))
        return PICOMESH_ERR(object_ptr, "time_clock_create: class accessor failed", _kr);
    /* A service dependency is acquired once and cached for the connection
     * (remote) / process (in-process) lifetime — no per-call create. */
    return rpc_object_acquire(ctx, _kr.value, "time_clock");
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

/* ---- time: minvoke table ------------------------------------ */

struct time_minvoke_row { const char *name; minvoke_fn fn; };

static const struct time_minvoke_row time_minvoke_rows[] = {
    {"time_clock_now_ms", time_clock_now_ms_minvoke},
    {"time_clock_sleep_ms", time_clock_sleep_ms_minvoke}
};

static minvoke_fn time_minvoke_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(time_minvoke_rows) / sizeof(time_minvoke_rows[0]); ++i)
        if (strcmp(time_minvoke_rows[i].name, qname) == 0)
            return time_minvoke_rows[i].fn;
    return NULL;
}

/* ---- time: per-method parameter signatures (runtime reflection) -- */

static const struct jinvoke_param time_clock_sleep_ms_params[] = {
    {"ms", "uint32_t"}
};
struct time_params_row { const char *name; struct jinvoke_params params; };

static const struct time_params_row time_params_rows[] = {
    {"time_clock_now_ms", {NULL, 0}},
    {"time_clock_sleep_ms", {time_clock_sleep_ms_params, 1}}
};

static const struct jinvoke_params *time_params_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(time_params_rows) / sizeof(time_params_rows[0]); ++i)
        if (strcmp(time_params_rows[i].name, qname) == 0)
            return &time_params_rows[i].params;
    return NULL;
}
/* ---- time: class name → accessor (lazy) ---------------------- */

static struct class_ptr_result time_accessor_lookup(const char *name)
{
    if (strcmp(name, "time_clock") == 0) return time_clock_class_get();
    return PICOMESH_OK(class_ptr, NULL);
}

/* ---- time: slot → skel, name-keyed static data --------------- */

struct time_skel_row { const char *name; rpc_skel_fn fn; };

static const struct time_skel_row time_skel_rows[] = {
    {"time_clock_now_ms", time_clock_now_ms_skel},
    {"time_clock_sleep_ms", time_clock_sleep_ms_skel}
};

static rpc_skel_fn time_skel_lookup(const char *name)
{
    /* rpc_skel_for has already resolved the slot to its qname (the only
     * Result-returning step), so this hook is a pure name→fn lookup that
     * never has to swallow an error. */
    for (size_t i = 0; i < sizeof(time_skel_rows) / sizeof(time_skel_rows[0]); ++i)
        if (strcmp(time_skel_rows[i].name, name) == 0)
            return time_skel_rows[i].fn;
    return NULL;
}

/* ---- time: registration entry point (called from the driver for
 *      config-ACTIVATED plugins only — registration is activation) ---- */

struct picomesh_void_result picomesh_plugin_time_register(void)
{
    struct picomesh_void_result _ar = class_add_accessor_lookup(time_accessor_lookup);
    PICOMESH_RETURN_IF_ERR(picomesh_void, _ar,
                           "picomesh_plugin_time_register: add accessor lookup");
    rpc_add_skel_lookup(time_skel_lookup);
    jinvoke_add_lookup(time_jinvoke_lookup);
    minvoke_add_lookup(time_minvoke_lookup);
    jinvoke_params_add_lookup(time_params_lookup);
    { struct class_ptr_result reg = time_clock_class_get();
      PICOMESH_RETURN_IF_ERR(picomesh_void, reg, "time register: prewarm time_clock_class_get"); }
    return PICOMESH_OK_VOID();
}
