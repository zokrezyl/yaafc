/* GENERATED — do not edit. */
/* Internal codegen-only header for plugin `storage`.
 * NEVER include this from outside src/yaafc/plugins/storage/. */
#ifndef YAAFC_STORAGE_INTERNAL_H
#define YAAFC_STORAGE_INTERNAL_H

#include <yaafc/plugin/storage/storage.h>

typedef struct yaafc_int_result (*storage_kv_set_fn)(struct ctx *, struct object *, struct yheaders *, const char *, const char *);
typedef struct yaafc_string_result (*storage_kv_get_fn)(struct ctx *, struct object *, struct yheaders *, const char *);
typedef struct yaafc_size_result (*storage_kv_count_fn)(struct ctx *, struct object *, struct yheaders *);
typedef struct yaafc_int_result (*storage_set_fn)(struct ctx *, struct object *, struct yheaders *, const char *, const char *, const char *);
typedef struct yaafc_string_result (*storage_get_fn)(struct ctx *, struct object *, struct yheaders *, const char *, const char *);
typedef struct yaafc_int_result (*storage_exists_fn)(struct ctx *, struct object *, struct yheaders *, const char *, const char *);
typedef struct yaafc_int_result (*storage_del_fn)(struct ctx *, struct object *, struct yheaders *, const char *, const char *);
typedef struct yaafc_size_result (*storage_count_fn)(struct ctx *, struct object *, struct yheaders *, const char *);

#endif
