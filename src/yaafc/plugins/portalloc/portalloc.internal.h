/* GENERATED — do not edit. */
/* Internal codegen-only header for plugin `portalloc`.
 * NEVER include this from outside src/yaafc/plugins/portalloc/. */
#ifndef YAAFC_PORTALLOC_INTERNAL_H
#define YAAFC_PORTALLOC_INTERNAL_H

#include <yaafc/plugin/portalloc/portalloc.h>

typedef struct yaafc_uint32_result (*portalloc_store_allocate_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_int_result (*portalloc_store_release_fn)(struct ctx *, struct object *, uint32_t);
typedef struct yaafc_size_result (*portalloc_store_count_used_fn)(struct ctx *, struct object *);

#endif
