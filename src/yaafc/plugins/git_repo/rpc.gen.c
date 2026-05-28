/* GENERATED — do not edit. */
#include <yaafc/yclass/rpc.h>
#include <yaafc/yclass/jinvoke.h>
#include <yaafc/yjson/yjson.h>
#include <yaafc/ycore/result.h>
#include <yaafc/ycore/ytrace.h>
#include <yaafc/yclass/class.h>
#include "git_repo.internal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t git_repo_store_make_skel(const void *_body, size_t _body_len,
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
    struct yaafc_uint32_result _r = git_repo_store_make(&_local, _obj, _v1);
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {
        yaafc_error_print(stderr, "[skel] git_repo_store_make", _r.error);
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

static size_t git_repo_store_delete_skel(const void *_body, size_t _body_len,
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
    struct yaafc_int_result _r = git_repo_store_delete(&_local, _obj, _v1);
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {
        yaafc_error_print(stderr, "[skel] git_repo_store_delete", _r.error);
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

static size_t git_repo_store_owner_of_skel(const void *_body, size_t _body_len,
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
    struct yaafc_uint32_result _r = git_repo_store_owner_of(&_local, _obj, _v1);
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {
        yaafc_error_print(stderr, "[skel] git_repo_store_owner_of", _r.error);
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

static size_t git_repo_store_count_for_owner_skel(const void *_body, size_t _body_len,
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
    struct yaafc_size_result _r = git_repo_store_count_for_owner(&_local, _obj, _v1);
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {
        yaafc_error_print(stderr, "[skel] git_repo_store_count_for_owner", _r.error);
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

static size_t git_repo_store_count_total_skel(const void *_body, size_t _body_len,
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
    struct yaafc_size_result _r = git_repo_store_count_total(&_local, _obj);
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {
        yaafc_error_print(stderr, "[skel] git_repo_store_count_total", _r.error);
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

static int git_repo_store_make_jinvoke(struct object *_obj, const struct yjson_value *_args,
                          struct yjson_writer *_result, char *_err, size_t _err_cap)
{
    uint32_t _a0 = (uint32_t)yjson_as_int(yjson_array_at(_args, 0), 0);
    struct ctx _ctx = {0};
    struct yaafc_uint32_result _r = git_repo_store_make(&_ctx, _obj, _a0);
    if (YAAFC_IS_ERR(_r)) {
        snprintf(_err, _err_cap, "%s: %s", "git_repo_store_make",
                 _r.error.msg ? _r.error.msg : "<no message>");
        yaafc_error_destroy(_r.error);
        return -1;
    }
    yjson_w_int(_result, (int64_t)_r.value);
    return 0;
}

static int git_repo_store_delete_jinvoke(struct object *_obj, const struct yjson_value *_args,
                          struct yjson_writer *_result, char *_err, size_t _err_cap)
{
    uint32_t _a0 = (uint32_t)yjson_as_int(yjson_array_at(_args, 0), 0);
    struct ctx _ctx = {0};
    struct yaafc_int_result _r = git_repo_store_delete(&_ctx, _obj, _a0);
    if (YAAFC_IS_ERR(_r)) {
        snprintf(_err, _err_cap, "%s: %s", "git_repo_store_delete",
                 _r.error.msg ? _r.error.msg : "<no message>");
        yaafc_error_destroy(_r.error);
        return -1;
    }
    yjson_w_int(_result, (int64_t)_r.value);
    return 0;
}

static int git_repo_store_owner_of_jinvoke(struct object *_obj, const struct yjson_value *_args,
                          struct yjson_writer *_result, char *_err, size_t _err_cap)
{
    uint32_t _a0 = (uint32_t)yjson_as_int(yjson_array_at(_args, 0), 0);
    struct ctx _ctx = {0};
    struct yaafc_uint32_result _r = git_repo_store_owner_of(&_ctx, _obj, _a0);
    if (YAAFC_IS_ERR(_r)) {
        snprintf(_err, _err_cap, "%s: %s", "git_repo_store_owner_of",
                 _r.error.msg ? _r.error.msg : "<no message>");
        yaafc_error_destroy(_r.error);
        return -1;
    }
    yjson_w_int(_result, (int64_t)_r.value);
    return 0;
}

static int git_repo_store_count_for_owner_jinvoke(struct object *_obj, const struct yjson_value *_args,
                          struct yjson_writer *_result, char *_err, size_t _err_cap)
{
    uint32_t _a0 = (uint32_t)yjson_as_int(yjson_array_at(_args, 0), 0);
    struct ctx _ctx = {0};
    struct yaafc_size_result _r = git_repo_store_count_for_owner(&_ctx, _obj, _a0);
    if (YAAFC_IS_ERR(_r)) {
        snprintf(_err, _err_cap, "%s: %s", "git_repo_store_count_for_owner",
                 _r.error.msg ? _r.error.msg : "<no message>");
        yaafc_error_destroy(_r.error);
        return -1;
    }
    yjson_w_int(_result, (int64_t)_r.value);
    return 0;
}

static int git_repo_store_count_total_jinvoke(struct object *_obj, const struct yjson_value *_args,
                          struct yjson_writer *_result, char *_err, size_t _err_cap)
{
    struct ctx _ctx = {0};
    struct yaafc_size_result _r = git_repo_store_count_total(&_ctx, _obj);
    if (YAAFC_IS_ERR(_r)) {
        snprintf(_err, _err_cap, "%s: %s", "git_repo_store_count_total",
                 _r.error.msg ? _r.error.msg : "<no message>");
        yaafc_error_destroy(_r.error);
        return -1;
    }
    yjson_w_int(_result, (int64_t)_r.value);
    return 0;
}

struct object_ptr_result git_repo_store_create(struct ctx *ctx)
{
    ydebug("class=git_repo_store");
    struct class_ptr_result _kr = git_repo_store_class_get();
    if (YAAFC_IS_ERR(_kr))
        return YAAFC_ERR(object_ptr, "git_repo_store_create: class accessor failed", _kr);
    const struct class *_klass = _kr.value;

    if (!ctx || !ctx->session)
        return object_alloc(_klass);

    rpc_session_translate_class(ctx->session, "git_repo_store");

    uint64_t _h = 0;
    const char *_name = "git_repo_store";
    if (rpc_call(ctx->session, RPC_OP_CREATE, 0, _name, strlen(_name),
                 &_h, sizeof(_h)) != sizeof(_h) || !_h)
        return YAAFC_ERR(object_ptr, "git_repo_store_create: remote create failed");

    void *_mem = calloc(1, sizeof(struct object) + sizeof(uint64_t));
    if (!_mem)
        return YAAFC_ERR(object_ptr, "git_repo_store_create: calloc(proxy) failed");
    struct object *_obj = _mem;
    *(const struct class **)_obj = _klass;
    *(uint64_t *)((char *)_obj + sizeof(*_obj)) = _h;
    return YAAFC_OK(object_ptr, _obj);
}


/* ---- git_repo: jinvoke table ------------------------------------ */

struct git_repo_jinvoke_row { const char *name; jinvoke_fn fn; };

static const struct git_repo_jinvoke_row git_repo_jinvoke_rows[] = {
    {"git_repo_store_make", git_repo_store_make_jinvoke},
    {"git_repo_store_delete", git_repo_store_delete_jinvoke},
    {"git_repo_store_owner_of", git_repo_store_owner_of_jinvoke},
    {"git_repo_store_count_for_owner", git_repo_store_count_for_owner_jinvoke},
    {"git_repo_store_count_total", git_repo_store_count_total_jinvoke}
};

static jinvoke_fn git_repo_jinvoke_lookup(const char *qname)
{
    for (size_t i = 0;
         i < sizeof(git_repo_jinvoke_rows) / sizeof(git_repo_jinvoke_rows[0]); ++i)
        if (strcmp(git_repo_jinvoke_rows[i].name, qname) == 0)
            return git_repo_jinvoke_rows[i].fn;
    return NULL;
}
/* ---- git_repo: class name → accessor (lazy) ---------------------- */

static struct class_ptr_result git_repo_accessor_lookup(const char *name)
{
    if (strcmp(name, "git_repo_store") == 0) return git_repo_store_class_get();
    return YAAFC_OK(class_ptr, NULL);
}

/* ---- git_repo: slot → skel, name-keyed static data --------------- */

struct git_repo_skel_row { const char *name; rpc_skel_fn fn; };

static const struct git_repo_skel_row git_repo_skel_rows[] = {
    {"git_repo_store_make", git_repo_store_make_skel},
    {"git_repo_store_delete", git_repo_store_delete_skel},
    {"git_repo_store_owner_of", git_repo_store_owner_of_skel},
    {"git_repo_store_count_for_owner", git_repo_store_count_for_owner_skel},
    {"git_repo_store_count_total", git_repo_store_count_total_skel}
};

static rpc_skel_fn git_repo_skel_lookup(method_slot slot)
{
    struct const_char_ptr_result nr = method_slot_name(slot);
    if (YAAFC_IS_ERR(nr)) { yaafc_error_destroy(nr.error); return NULL; }
    const char *name = nr.value;
    for (size_t i = 0; i < sizeof(git_repo_skel_rows) / sizeof(git_repo_skel_rows[0]); ++i)
        if (strcmp(git_repo_skel_rows[i].name, name) == 0)
            return git_repo_skel_rows[i].fn;
    return NULL;
}

/* ---- git_repo: install hooks before main ------------------------- */

__attribute__((constructor))
static void git_repo_install_hooks(void)
{
    struct yaafc_void_result _ar = class_add_accessor_lookup(git_repo_accessor_lookup);
    if (YAAFC_IS_ERR(_ar)) {
        yaafc_error_print(stderr, "git_repo_install_hooks", _ar.error);
        yaafc_error_destroy(_ar.error);
        abort();
    }
    rpc_add_skel_lookup(git_repo_skel_lookup);
    jinvoke_add_lookup(git_repo_jinvoke_lookup);
}
