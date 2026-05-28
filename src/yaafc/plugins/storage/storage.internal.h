/* GENERATED — do not edit. */
/* Internal codegen-only header for plugin `storage`.
 * NEVER include this from outside src/yaafc/plugins/storage/. */
#ifndef YAAFC_STORAGE_INTERNAL_H
#define YAAFC_STORAGE_INTERNAL_H

#include <yaafc/plugin/storage/storage.h>

typedef struct yaafc_int_result (*storage_kv_set_fn)(struct ctx *, struct object *, uint32_t, int32_t);
typedef struct yaafc_int_result (*storage_kv_get_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_size_result (*storage_kv_count_fn)(struct ctx *, struct object *);
typedef struct yaafc_int_result (*storage_sql_set_fn)(struct ctx *, struct object *, const char *, int64_t);
typedef struct yaafc_int64_result (*storage_sql_get_fn)(struct ctx *, struct object *, const char *);
typedef struct yaafc_int_result (*storage_sql_exists_fn)(struct ctx *, struct object *, const char *);
typedef struct yaafc_int_result (*storage_sql_del_fn)(struct ctx *, struct object *, const char *);
typedef struct yaafc_size_result (*storage_sql_count_fn)(struct ctx *, struct object *);

#endif
