/* json — thin C ABI over simdjson + a small JSON writer.
 *
 * Parsing goes through simdjson; we expose a borrowed-handle API so
 * C callers don't see any C++ details. Building JSON for outbound
 * responses goes through a hand-rolled writer (simdjson is parse-
 * only) — sufficient for the limited shapes yttp / cli emit.
 *
 * Lifetime:
 *
 *   struct json_doc *doc = json_parse(buf, len);
 *   const struct json_value *root = json_doc_root(doc);
 *   ... navigate / read ...
 *   json_doc_free(doc);
 *
 * Every borrowed `json_value *` is valid until `json_doc_free`. */

#ifndef PICOMESH_JSON_JSON_H
#define PICOMESH_JSON_JSON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct json_doc;
struct json_value;

enum json_kind {
    JSON_NULL,
    JSON_BOOL,
    JSON_INT,
    JSON_FLOAT,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
};

/* --- parse ----------------------------------------------------------- */

/* Parse `data[0..len)`. Returns NULL on parse error (caller can check
 * json_last_error if non-NULL needed). The doc owns every value
 * borrowed below. */
struct json_doc *json_parse(const char *data, size_t len);
void json_doc_free(struct json_doc *doc);
const struct json_value *json_doc_root(const struct json_doc *doc);

/* Last per-thread parse error message, or NULL. Heap-owned by the
 * library; do not free. */
const char *json_last_error(void);

/* --- type queries ---------------------------------------------------- */

enum json_kind json_kind(const struct json_value *v);
int json_is_null  (const struct json_value *v);
int json_is_bool  (const struct json_value *v);
int json_is_int   (const struct json_value *v);
int json_is_float (const struct json_value *v);
int json_is_string(const struct json_value *v);
int json_is_array (const struct json_value *v);
int json_is_object(const struct json_value *v);

/* --- scalar getters -------------------------------------------------- */

int     json_as_bool  (const struct json_value *v, int fallback);
int64_t json_as_int   (const struct json_value *v, int64_t fallback);
double  json_as_float (const struct json_value *v, double fallback);
/* Returned pointer is valid until json_doc_free. */
const char *json_as_string(const struct json_value *v, const char *fallback);

/* --- container access ----------------------------------------------- */

size_t json_array_size(const struct json_value *v);
const struct json_value *json_array_at(const struct json_value *v, size_t idx);

const struct json_value *json_object_get(const struct json_value *v, const char *key);

/* --- writer (hand-rolled; no simdjson involved) --------------------- */

struct json_writer;

struct json_writer *json_writer_new(void);
void json_writer_free(struct json_writer *w);

void json_writer_begin_object(struct json_writer *w);
void json_writer_end_object  (struct json_writer *w);
void json_writer_begin_array (struct json_writer *w);
void json_writer_end_array   (struct json_writer *w);

void json_writer_key   (struct json_writer *w, const char *key);
void json_writer_null  (struct json_writer *w);
void json_writer_bool  (struct json_writer *w, int v);
void json_writer_int   (struct json_writer *w, int64_t v);
void json_writer_float (struct json_writer *w, double v);
void json_writer_string(struct json_writer *w, const char *s);

/* Emit an already-serialized JSON fragment as a value, verbatim (no
 * quoting, no escaping). The caller guarantees `json` is well-formed JSON;
 * a NULL or empty fragment is written as `null`. Used to splice a value a
 * method already produced as JSON text (e.g. a list) into the response. */
void json_writer_raw   (struct json_writer *w, const char *json);

/* Borrow the assembled buffer (no copy). NUL-terminated. Valid until
 * the writer is freed or another write happens. `*len_out` (if non-
 * NULL) receives the length excluding NUL. */
const char *json_writer_data(struct json_writer *w, size_t *len_out);

#ifdef __cplusplus
}
#endif

#endif /* PICOMESH_JSON_JSON_H */
