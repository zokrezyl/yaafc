/* json — C ABI shim over simdjson + a small writer.
 *
 * Compiled as C++17. The C ABI is opaque: callers pass around
 * `struct json_doc *` / `struct json_value *` borrowed pointers.
 *
 * Internally we use simdjson's DOM parser (one parser + parsed doc
 * per json_doc). Borrowed values point into the doc; freeing the
 * doc invalidates all borrowed pointers.
 *
 * The writer is a tiny manual JSON serializer — simdjson doesn't
 * ship a stable builder API, and our outbound shapes are simple
 * enough that hand-rolling beats the dependency lift. */

#include <picomesh/json/json.h>

#include <simdjson.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace sj = simdjson;

/* ----------------------------------------------------------------- */
/* json_doc / json_value internals                                  */
/* ----------------------------------------------------------------- */

/* json_value is opaque to C. Under the hood it's a simdjson DOM
 * element — but simdjson DOM elements are values (not pointers), so
 * we store them in a deque-like container and hand out pointers to
 * stable slots.
 *
 * Strings come from simdjson as `std::string_view`. The DOM element
 * keeps them alive for the lifetime of the parsed buffer, so we
 * lazily NUL-terminate into our own arena when callers ask for a
 * C-string. */

struct json_doc;

struct json_value {
    sj::dom::element elem;
    /* Owning doc — child handles (object_get / array_at) are allocated
     * into doc->values so they're freed with the doc. */
    json_doc *doc = nullptr;
    /* lazy NUL-terminated cache for as_string */
    mutable std::string str_cache;
    mutable bool str_cached = false;
};

struct json_doc {
    sj::dom::parser parser;
    sj::padded_string buf;
    sj::dom::element root;

    /* Stable storage for json_value handles we've handed out. Using
     * deque would invalidate-never, but std::list is overkill — we
     * use a chunked vector. Pointer stability comes from never
     * resizing existing chunks. */
    std::vector<std::unique_ptr<json_value>> values;
};

/* Per-thread last-error string for the parse failure path. */
static thread_local std::string g_last_err;

static json_value *make_value(json_doc *doc, sj::dom::element e)
{
    auto v = std::make_unique<json_value>();
    v->elem = e;
    v->doc = doc;
    json_value *raw = v.get();
    doc->values.push_back(std::move(v));
    return raw;
}

/* ----------------------------------------------------------------- */
/* Parse                                                              */
/* ----------------------------------------------------------------- */

extern "C" struct json_doc *json_parse(const char *data, size_t len)
{
    auto doc = std::make_unique<json_doc>();
    /* simdjson needs a SIMDJSON_PADDING tail to vectorize the tail
     * load safely. padded_string copies + pads. */
    doc->buf = sj::padded_string(data, len);
    auto res = doc->parser.parse(doc->buf);
    if (res.error()) {
        g_last_err = sj::error_message(res.error());
        return nullptr;
    }
    doc->root = res.value();
    return doc.release();
}

extern "C" void json_doc_free(struct json_doc *doc)
{
    delete doc;
}

extern "C" const struct json_value *json_doc_root(const struct json_doc *doc)
{
    if (!doc) return nullptr;
    /* The root needs a stable json_value*; make_value mutates the
     * doc's vector, so cast away const for this one allocation. */
    auto *mut = const_cast<json_doc *>(doc);
    return make_value(mut, doc->root);
}

extern "C" const char *json_last_error(void)
{
    if (g_last_err.empty()) return nullptr;
    return g_last_err.c_str();
}

/* ----------------------------------------------------------------- */
/* Type queries / scalar getters                                      */
/* ----------------------------------------------------------------- */

extern "C" enum json_kind json_kind(const struct json_value *v)
{
    if (!v) return JSON_NULL;
    switch (v->elem.type()) {
    case sj::dom::element_type::ARRAY:    return JSON_ARRAY;
    case sj::dom::element_type::OBJECT:   return JSON_OBJECT;
    case sj::dom::element_type::STRING:   return JSON_STRING;
    case sj::dom::element_type::INT64:
    case sj::dom::element_type::UINT64:   return JSON_INT;
    case sj::dom::element_type::DOUBLE:   return JSON_FLOAT;
    case sj::dom::element_type::BOOL:     return JSON_BOOL;
    case sj::dom::element_type::NULL_VALUE:
    default:                              return JSON_NULL;
    }
}

extern "C" int json_is_null  (const struct json_value *v) { return json_kind(v) == JSON_NULL; }
extern "C" int json_is_bool  (const struct json_value *v) { return json_kind(v) == JSON_BOOL; }
extern "C" int json_is_int   (const struct json_value *v) { return json_kind(v) == JSON_INT; }
extern "C" int json_is_float (const struct json_value *v) { return json_kind(v) == JSON_FLOAT; }
extern "C" int json_is_string(const struct json_value *v) { return json_kind(v) == JSON_STRING; }
extern "C" int json_is_array (const struct json_value *v) { return json_kind(v) == JSON_ARRAY; }
extern "C" int json_is_object(const struct json_value *v) { return json_kind(v) == JSON_OBJECT; }

extern "C" int json_as_bool(const struct json_value *v, int fallback)
{
    if (!v) return fallback;
    bool b;
    if (v->elem.get(b) == sj::SUCCESS) return b ? 1 : 0;
    int64_t i;
    if (v->elem.get(i) == sj::SUCCESS) return i != 0;
    return fallback;
}

extern "C" int64_t json_as_int(const struct json_value *v, int64_t fallback)
{
    if (!v) return fallback;
    int64_t i;
    if (v->elem.get(i) == sj::SUCCESS) return i;
    uint64_t u;
    if (v->elem.get(u) == sj::SUCCESS) return (int64_t)u;
    double d;
    if (v->elem.get(d) == sj::SUCCESS) return (int64_t)d;
    bool b;
    if (v->elem.get(b) == sj::SUCCESS) return b ? 1 : 0;
    return fallback;
}

extern "C" double json_as_float(const struct json_value *v, double fallback)
{
    if (!v) return fallback;
    double d;
    if (v->elem.get(d) == sj::SUCCESS) return d;
    int64_t i;
    if (v->elem.get(i) == sj::SUCCESS) return (double)i;
    return fallback;
}

extern "C" const char *json_as_string(const struct json_value *v, const char *fallback)
{
    if (!v) return fallback;
    std::string_view sv;
    if (v->elem.get(sv) != sj::SUCCESS) return fallback;
    if (!v->str_cached) {
        v->str_cache.assign(sv.data(), sv.size());
        v->str_cached = true;
    }
    return v->str_cache.c_str();
}

/* ----------------------------------------------------------------- */
/* Container access                                                   */
/* ----------------------------------------------------------------- */

extern "C" size_t json_array_size(const struct json_value *v)
{
    if (!v) return 0;
    sj::dom::array arr;
    if (v->elem.get(arr) != sj::SUCCESS) return 0;
    return arr.size();
}

extern "C" const struct json_value *json_array_at(const struct json_value *v, size_t idx)
{
    if (!v) return nullptr;
    sj::dom::array arr;
    if (v->elem.get(arr) != sj::SUCCESS) return nullptr;
    if (idx >= arr.size()) return nullptr;
    auto e = arr.at(idx);
    if (e.error()) return nullptr;
    /* Child handle is owned by the same doc we descend from, so it is
     * freed with json_doc_free — no thread-local arena (which used to
     * grow without bound, one entry per call, and leaked). */
    if (!v->doc) return nullptr;
    return make_value(v->doc, e.value());
}

extern "C" const struct json_value *json_object_get(const struct json_value *v, const char *key)
{
    if (!v || !key) return nullptr;
    sj::dom::object obj;
    if (v->elem.get(obj) != sj::SUCCESS) return nullptr;
    auto e = obj[key];
    if (e.error()) return nullptr;
    /* Owned by the descending doc → freed with json_doc_free. (Was a
     * never-cleared thread-local arena that leaked one handle per call.) */
    if (!v->doc) return nullptr;
    return make_value(v->doc, e.value());
}

/* ----------------------------------------------------------------- */
/* Writer                                                              */
/* ----------------------------------------------------------------- */
/*
 * Tracks containers on a small stack so we can emit commas correctly.
 * Each "slot" position remembers whether something has been written
 * inside the current container (controls leading-comma insertion).
 *
 * Container kinds:
 *   'o' = object, expect alternating key / value
 *   'a' = array, expect values
 *   ' ' = top level (one value only)
 */

struct json_writer {
    std::string out;
    struct frame { char kind; int has_item; int expects_value; };
    std::vector<frame> stack;
};

static void w_open(json_writer *w)
{
    if (w->stack.empty()) {
        w->stack.push_back({' ', 0, 1});
        return;
    }
    auto &top = w->stack.back();
    if (top.kind == 'a') {
        if (top.has_item) w->out.push_back(',');
        top.has_item = 1;
    } else if (top.kind == 'o') {
        if (!top.expects_value) {
            /* We're at a key position — caller should have called _key
             * first. Treat as an error: write an empty key. */
            w->out.append("\"\":");
            top.expects_value = 1;
        } else {
            top.expects_value = 0;
            top.has_item = 1;
        }
    }
}

static void w_close_value(json_writer *w)
{
    if (w->stack.empty()) return;
    auto &top = w->stack.back();
    if (top.kind == 'o') {
        /* Just wrote a value; next is key again. */
        top.expects_value = 0;
    }
}

static void escape_string(std::string &out, const char *s)
{
    out.push_back('"');
    if (!s) { out.push_back('"'); return; }
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        unsigned char c = *p;
        switch (c) {
        case '"':  out.append("\\\""); break;
        case '\\': out.append("\\\\"); break;
        case '\b': out.append("\\b");  break;
        case '\f': out.append("\\f");  break;
        case '\n': out.append("\\n");  break;
        case '\r': out.append("\\r");  break;
        case '\t': out.append("\\t");  break;
        default:
            if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out.append(buf);
            } else {
                out.push_back((char)c);
            }
        }
    }
    out.push_back('"');
}

extern "C" struct json_writer *json_writer_new(void)
{
    return new (std::nothrow) json_writer();
}

extern "C" void json_writer_free(struct json_writer *w)
{
    delete w;
}

extern "C" void json_writer_begin_object(struct json_writer *w)
{
    if (!w) return;
    w_open(w);
    w->out.push_back('{');
    w->stack.push_back({'o', 0, 0});
}

extern "C" void json_writer_end_object(struct json_writer *w)
{
    if (!w || w->stack.empty()) return;
    w->out.push_back('}');
    w->stack.pop_back();
    w_close_value(w);
}

extern "C" void json_writer_begin_array(struct json_writer *w)
{
    if (!w) return;
    w_open(w);
    w->out.push_back('[');
    w->stack.push_back({'a', 0, 1});
}

extern "C" void json_writer_end_array(struct json_writer *w)
{
    if (!w || w->stack.empty()) return;
    w->out.push_back(']');
    w->stack.pop_back();
    w_close_value(w);
}

extern "C" void json_writer_key(struct json_writer *w, const char *key)
{
    if (!w || w->stack.empty()) return;
    auto &top = w->stack.back();
    if (top.kind != 'o') return;
    if (top.has_item) w->out.push_back(',');
    escape_string(w->out, key);
    w->out.push_back(':');
    top.expects_value = 1;
}

extern "C" void json_writer_null(struct json_writer *w)
{
    if (!w) return; w_open(w); w->out.append("null"); w_close_value(w);
}

extern "C" void json_writer_bool(struct json_writer *w, int v)
{
    if (!w) return; w_open(w);
    w->out.append(v ? "true" : "false");
    w_close_value(w);
}

extern "C" void json_writer_int(struct json_writer *w, int64_t v)
{
    if (!w) return; w_open(w);
    char buf[32]; snprintf(buf, sizeof(buf), "%lld", (long long)v);
    w->out.append(buf);
    w_close_value(w);
}

extern "C" void json_writer_float(struct json_writer *w, double v)
{
    if (!w) return; w_open(w);
    char buf[64]; snprintf(buf, sizeof(buf), "%g", v);
    w->out.append(buf);
    w_close_value(w);
}

extern "C" void json_writer_string(struct json_writer *w, const char *s)
{
    if (!w) return; w_open(w);
    escape_string(w->out, s ? s : "");
    w_close_value(w);
}

extern "C" void json_writer_raw(struct json_writer *w, const char *json)
{
    if (!w) return; w_open(w);
    w->out.append(json && *json ? json : "null");
    w_close_value(w);
}

extern "C" const char *json_writer_data(struct json_writer *w, size_t *len_out)
{
    if (!w) { if (len_out) *len_out = 0; return ""; }
    if (len_out) *len_out = w->out.size();
    return w->out.c_str();
}
