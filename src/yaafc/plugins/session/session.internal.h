/* GENERATED — do not edit. */
/* Internal codegen-only header for plugin `session`.
 * NEVER include this from outside src/yaafc/plugins/session/. */
#ifndef YAAFC_SESSION_INTERNAL_H
#define YAAFC_SESSION_INTERNAL_H

#include <yaafc/plugin/session/session.h>

typedef struct yaafc_uint32_result (*session_store_start_fn)(struct ctx *, struct object *, uint32_t, uint32_t);
typedef struct yaafc_uint32_result (*session_store_lookup_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_int_result (*session_store_destroy_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_size_result (*session_store_count_active_fn)(struct ctx *, struct object *);

#endif
