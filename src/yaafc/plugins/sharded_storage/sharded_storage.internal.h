/* GENERATED — do not edit. */
/* Internal codegen-only header for plugin `sharded_storage`.
 * NEVER include this from outside src/yaafc/plugins/sharded_storage/. */
#ifndef YAAFC_SHARDED_STORAGE_INTERNAL_H
#define YAAFC_SHARDED_STORAGE_INTERNAL_H

#include <yaafc/plugin/sharded_storage/sharded_storage.h>

typedef struct yaafc_int_result (*sharded_storage_db_set_fn)(struct ctx *, struct object *, struct yheaders *, const char *, const char *, const char *);
typedef struct yaafc_string_result (*sharded_storage_db_get_fn)(struct ctx *, struct object *, struct yheaders *, const char *, const char *);
typedef struct yaafc_int_result (*sharded_storage_db_exists_fn)(struct ctx *, struct object *, struct yheaders *, const char *, const char *);
typedef struct yaafc_int_result (*sharded_storage_db_del_fn)(struct ctx *, struct object *, struct yheaders *, const char *, const char *);
typedef struct yaafc_size_result (*sharded_storage_db_count_fn)(struct ctx *, struct object *, struct yheaders *, const char *);

#endif
