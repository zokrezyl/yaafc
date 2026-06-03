/* GENERATED — do not edit. */
/* Internal codegen-only header for plugin `runner_agent`.
 * NEVER include this from outside src/picomesh/plugins/runner_agent/. */
#ifndef PICOMESH_RUNNER_AGENT_INTERNAL_H
#define PICOMESH_RUNNER_AGENT_INTERNAL_H

#include <picomesh/plugin/runner_agent/runner_agent.h>

typedef struct picomesh_json_result (*runner_agent_runner_agent_create_token_fn)(struct ctx *, struct object *, struct yheaders *, const char *, const char *);
typedef struct picomesh_uint32_result (*runner_agent_runner_agent_lookup_token_fn)(struct ctx *, struct object *, struct yheaders *, const char *);
typedef struct picomesh_int_result (*runner_agent_runner_agent_revoke_token_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t);
typedef struct picomesh_uint32_result (*runner_agent_runner_agent_register_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t, const char *, const char *, const char *, const char *);
typedef struct picomesh_int_result (*runner_agent_runner_agent_heartbeat_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t, const char *);
typedef struct picomesh_json_result (*runner_agent_runner_agent_get_fn)(struct ctx *, struct object *, struct yheaders *, uint32_t);
typedef struct picomesh_json_result (*runner_agent_runner_agent_list_fn)(struct ctx *, struct object *, struct yheaders *, int64_t, int64_t);
typedef struct picomesh_json_result (*runner_agent_runner_agent_list_all_fn)(struct ctx *, struct object *, struct yheaders *);
typedef struct picomesh_size_result (*runner_agent_runner_agent_count_active_fn)(struct ctx *, struct object *, struct yheaders *);

#endif
