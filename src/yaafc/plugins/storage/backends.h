/* Private backend interface for the storage plugin.
 *
 * Two file-local backends (sqlite, mdbx) implement the same ops shape
 * over the same `struct storage_data`. The data struct embeds a union
 * of per-backend state so that no heap allocation is needed for the
 * connection handle.
 *
 * NEVER include this from outside src/yaafc/plugins/storage/. */
#ifndef YAAFC_STORAGE_BACKENDS_H
#define YAAFC_STORAGE_BACKENDS_H

#include <sqlite3.h>

#include <stddef.h>
#include <stdint.h>

struct storage_data;

enum storage_backend {
    STORAGE_BACKEND_UNSET = 0,
    STORAGE_BACKEND_SQLITE,
    STORAGE_BACKEND_MDBX,
};

/* Result codes returned by backend ops. Mirrors the public storage_*
 * shape one level down — the slot impls in storage.c translate these
 * into Result types. */
enum storage_rc {
    STORAGE_RC_OK = 0,
    STORAGE_RC_NOT_FOUND,
    STORAGE_RC_BAD_CONTEXT,
    STORAGE_RC_OPEN_FAILED,
    STORAGE_RC_INTERNAL,
};

struct backend_ops {
    /* set(context, key, value) → STORAGE_RC_OK on success. `value` is an
     * opaque NUL-terminated string/byte sequence. */
    enum storage_rc (*set)(struct storage_data *d, const char *context,
                           const char *key, const char *value);
    /* get(context, key, *out) → STORAGE_RC_NOT_FOUND if absent. On OK,
     * *out is a heap-allocated NUL-terminated copy the caller frees. */
    enum storage_rc (*get)(struct storage_data *d, const char *context,
                           const char *key, char **out);
    /* exists(context, key) → STORAGE_RC_OK with *out=1/0. */
    enum storage_rc (*exists)(struct storage_data *d, const char *context,
                              const char *key, int *out);
    /* del(context, key) → STORAGE_RC_OK with *out=1 if removed, 0 if absent. */
    enum storage_rc (*del)(struct storage_data *d, const char *context,
                           const char *key, int *out);
    /* count(context) → row/entry count in *out. */
    enum storage_rc (*count)(struct storage_data *d, const char *context,
                             size_t *out);
};

/* Per-backend op-tables; defined in backend_sqlite.c / backend_mdbx.c. */
const struct backend_ops *storage_backend_sqlite_ops(void);
const struct backend_ops *storage_backend_mdbx_ops(void);

/* Canonical `struct storage_data` lives in storage.c — it carries the
 * yclass annotation that the codegen reads to determine class data
 * size. Backend files see the full definition because they are
 * #included by storage.c after the struct definition. */
struct storage_data;

/* Context-name validation — both backends accept the same shape so the
 * caller's choice is portable across backends. Returns 1 if valid. */
int storage_context_is_valid(const char *context);

#endif
