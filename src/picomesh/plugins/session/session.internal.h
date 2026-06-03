/* GENERATED — do not edit. */
/* Internal codegen-only header for plugin `session`.
 * NEVER include this from outside src/picomesh/plugins/session/. */
#ifndef PICOMESH_SESSION_INTERNAL_H
#define PICOMESH_SESSION_INTERNAL_H

#include <picomesh/plugin/session/session.h>

typedef struct picomesh_string_result (*session_session_start_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t, const char *, const char *);
typedef struct picomesh_string_result (*session_session_jwt_fn)(struct ctx *, struct object *, struct yheaders *, const char *);
typedef struct picomesh_uint32_result (*session_session_lookup_fn)(struct ctx *, struct object *, struct yheaders *, const char *);
typedef struct picomesh_int_result (*session_session_destroy_fn)(struct ctx *, struct object *, struct yheaders *, const char *);
typedef struct picomesh_size_result (*session_session_count_active_fn)(struct ctx *, struct object *, struct yheaders *);
typedef struct picomesh_json_result (*session_session_list_fn)(struct ctx *, struct object *, struct yheaders *, int64_t, int64_t);
typedef struct picomesh_json_result (*session_session_list_all_fn)(struct ctx *, struct object *, struct yheaders *);

#endif
