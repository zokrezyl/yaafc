/* registry — service registry for mesh discovery.
 *
 * In-memory `service_name → {instance_id, host, port}` map. Services
 * register their bound address here; consumers look a service up by name
 * to discover its current host:port. Pure data store — no health checks,
 * no persistence (state is lost on restart, by design; the mesh re-spawns
 * and every node re-registers on boot).
 *
 * The registry is one of only TWO nodes with a FIXED address (the other is
 * the mesh control parent). Its address is injected into every child's
 * config as a global block, so a freshly spawned node can reach the
 * registry before it knows where anything else lives — including portalloc.
 *
 * State is PROCESS-WIDE, not per-object: the RPC server allocates a fresh
 * receiver object for every client's CREATE, so a per-object table would be
 * invisible to the next client. The registrations therefore live in one
 * mutex-guarded singleton — the same shape the trace collector uses for its
 * span store. (The mutex is uncontended under the single worker the registry
 * runs with, and correct should it ever scale out.)
 *
 * Methods:
 *   register_service(name, instance_id, host, port) → 1
 *   deregister_service(name, instance_id)           → 1 ok / 0 unknown
 *   resolve(name)                                   → "host:port" ("" if unknown)
 *   discover_service(name)                          → JSON {service_name, instances:[…]}
 *   list_services()                                 → JSON [{service_name, instances:[…]}]
 *   count()                                         → number of live instances */

#include <picomesh/core/result.h>
#include <picomesh/core/ytrace.h>
#include <picomesh/picoclass/class.h>
#include <picomesh/json/json.h>

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REGISTRY_MAX_ENTRIES 256
#define REGISTRY_NAME_MAX 64
#define REGISTRY_INSTANCE_MAX 96
#define REGISTRY_HOST_MAX 64

struct registry_entry {
    char name[REGISTRY_NAME_MAX];
    char instance_id[REGISTRY_INSTANCE_MAX];
    char host[REGISTRY_HOST_MAX];
    uint32_t port;
    int used;
};

/* The class object carries no state — all of it is process-wide below. */
struct PICOMESH_CLASS_ANNOTATE("class@registry:registry") registry_registry_data {
    int placeholder;
};

struct registry_state {
    pthread_mutex_t mu;
    struct registry_entry entries[REGISTRY_MAX_ENTRIES];
    size_t count;
};

/* The one process-wide registry table, shared by every receiver object. */
static struct registry_state *registry_state(void)
{
    static struct registry_state state = {.mu = PTHREAD_MUTEX_INITIALIZER};
    return &state;
}

/* Find the slot holding (name, instance_id), or -1. Caller holds the lock. */
static int registry_find(struct registry_state *state, const char *name,
                         const char *instance_id)
{
    for (size_t i = 0; i < REGISTRY_MAX_ENTRIES; ++i) {
        if (state->entries[i].used &&
            strcmp(state->entries[i].name, name) == 0 &&
            strcmp(state->entries[i].instance_id, instance_id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

PICOMESH_CLASS_ANNOTATE("override@registry:registry:registry_register_service")
struct picomesh_int_result registry_registry_register_service_impl(struct ctx *ctx, struct object *obj,
                                                                   struct yheaders *hdrs,
                                                                   const char *name, const char *instance_id,
                                                                   const char *host, uint32_t port)
{
    (void)ctx; (void)obj; (void)hdrs;
    if (!name || !*name) return PICOMESH_ERR(picomesh_int, "registry_register: empty service name");
    if (!instance_id || !*instance_id) instance_id = name;
    if (!host || !*host) host = "127.0.0.1";
    struct registry_state *state = registry_state();
    pthread_mutex_lock(&state->mu);

    /* Re-registering (name, instance_id) refreshes its address. */
    int slot = registry_find(state, name, instance_id);
    if (slot < 0) {
        for (size_t i = 0; i < REGISTRY_MAX_ENTRIES; ++i) {
            if (!state->entries[i].used) { slot = (int)i; break; }
        }
        if (slot < 0) {
            pthread_mutex_unlock(&state->mu);
            return PICOMESH_ERR(picomesh_int, "registry_register: table full");
        }
        state->entries[slot].used = 1;
        state->count++;
    }
    snprintf(state->entries[slot].name, sizeof(state->entries[slot].name), "%s", name);
    snprintf(state->entries[slot].instance_id, sizeof(state->entries[slot].instance_id), "%s", instance_id);
    snprintf(state->entries[slot].host, sizeof(state->entries[slot].host), "%s", host);
    state->entries[slot].port = port;
    pthread_mutex_unlock(&state->mu);
    yinfo("registry: '%s' instance '%s' → %s:%u", name, instance_id, host, port);
    return PICOMESH_OK(picomesh_int, 1);
}

PICOMESH_CLASS_ANNOTATE("override@registry:registry:registry_deregister_service")
struct picomesh_int_result registry_registry_deregister_service_impl(struct ctx *ctx, struct object *obj,
                                                                     struct yheaders *hdrs,
                                                                     const char *name, const char *instance_id)
{
    (void)ctx; (void)obj; (void)hdrs;
    if (!name || !*name) return PICOMESH_OK(picomesh_int, 0);
    if (!instance_id || !*instance_id) instance_id = name;
    struct registry_state *state = registry_state();
    pthread_mutex_lock(&state->mu);
    int slot = registry_find(state, name, instance_id);
    if (slot < 0) {
        pthread_mutex_unlock(&state->mu);
        return PICOMESH_OK(picomesh_int, 0);
    }
    state->entries[slot].used = 0;
    state->count--;
    pthread_mutex_unlock(&state->mu);
    yinfo("registry: deregistered '%s' instance '%s'", name, instance_id);
    return PICOMESH_OK(picomesh_int, 1);
}

/* Resolve a service name to a concrete "host:port" string — the framework's
 * port: auto edge. Returns the most-recently-registered live instance. An
 * unknown service yields an empty string (the caller retries until the
 * producer has registered), NOT an error. */
PICOMESH_CLASS_ANNOTATE("override@registry:registry:registry_resolve")
struct picomesh_string_result registry_registry_resolve_impl(struct ctx *ctx, struct object *obj,
                                                             struct yheaders *hdrs, const char *name)
{
    (void)ctx; (void)obj; (void)hdrs;
    struct registry_state *state = registry_state();
    char buf[REGISTRY_HOST_MAX + 16];
    buf[0] = 0;
    pthread_mutex_lock(&state->mu);
    if (name && *name) {
        for (size_t i = 0; i < REGISTRY_MAX_ENTRIES; ++i) {
            if (state->entries[i].used && strcmp(state->entries[i].name, name) == 0) {
                snprintf(buf, sizeof(buf), "%s:%u", state->entries[i].host, state->entries[i].port);
                /* keep scanning: last write wins */
            }
        }
    }
    pthread_mutex_unlock(&state->mu);
    char *out = strdup(buf);
    if (!out) return PICOMESH_ERR(picomesh_string, "registry_resolve: out of memory");
    return PICOMESH_OK(picomesh_string, out);
}

/* Emit the instances of `name` as a JSON array onto an open writer. Caller
 * holds the lock. */
static void registry_write_instances(struct json_writer *writer,
                                     struct registry_state *state, const char *name)
{
    json_writer_begin_array(writer);
    for (size_t i = 0; i < REGISTRY_MAX_ENTRIES; ++i) {
        if (!state->entries[i].used || strcmp(state->entries[i].name, name) != 0) continue;
        json_writer_begin_object(writer);
        json_writer_key(writer, "instance_id"); json_writer_string(writer, state->entries[i].instance_id);
        json_writer_key(writer, "host");        json_writer_string(writer, state->entries[i].host);
        json_writer_key(writer, "port");        json_writer_int(writer, (int64_t)state->entries[i].port);
        json_writer_end_object(writer);
    }
    json_writer_end_array(writer);
}

PICOMESH_CLASS_ANNOTATE("override@registry:registry:registry_discover_service")
struct picomesh_json_result registry_registry_discover_service_impl(struct ctx *ctx, struct object *obj,
                                                                    struct yheaders *hdrs, const char *name)
{
    (void)ctx; (void)obj; (void)hdrs;
    if (!name) name = "";
    struct registry_state *state = registry_state();
    struct json_writer *writer = json_writer_new();
    if (!writer) return PICOMESH_ERR(picomesh_json, "registry_discover: writer alloc failed");
    pthread_mutex_lock(&state->mu);
    size_t total = 0;
    for (size_t i = 0; i < REGISTRY_MAX_ENTRIES; ++i) {
        if (state->entries[i].used && strcmp(state->entries[i].name, name) == 0) total++;
    }
    json_writer_begin_object(writer);
    json_writer_key(writer, "service_name"); json_writer_string(writer, name);
    json_writer_key(writer, "instances");    registry_write_instances(writer, state, name);
    json_writer_key(writer, "total_instances"); json_writer_int(writer, (int64_t)total);
    json_writer_end_object(writer);
    pthread_mutex_unlock(&state->mu);
    size_t len = 0;
    const char *jdata = json_writer_data(writer, &len);
    char *out = strdup(jdata ? jdata : "{}");
    json_writer_free(writer);
    if (!out) return PICOMESH_ERR(picomesh_json, "registry_discover: out of memory");
    return PICOMESH_OK(picomesh_json, out);
}

PICOMESH_CLASS_ANNOTATE("override@registry:registry:registry_list_services")
struct picomesh_json_result registry_registry_list_services_impl(struct ctx *ctx, struct object *obj,
                                                                 struct yheaders *hdrs)
{
    (void)ctx; (void)obj; (void)hdrs;
    struct registry_state *state = registry_state();
    struct json_writer *writer = json_writer_new();
    if (!writer) return PICOMESH_ERR(picomesh_json, "registry_list: writer alloc failed");
    pthread_mutex_lock(&state->mu);
    json_writer_begin_array(writer);
    /* One object per distinct service name: emit at the lowest slot carrying
     * that name so each name appears once. */
    for (size_t i = 0; i < REGISTRY_MAX_ENTRIES; ++i) {
        if (!state->entries[i].used) continue;
        int first = 1;
        for (size_t j = 0; j < i; ++j) {
            if (state->entries[j].used && strcmp(state->entries[j].name, state->entries[i].name) == 0) {
                first = 0; break;
            }
        }
        if (!first) continue;
        json_writer_begin_object(writer);
        json_writer_key(writer, "service_name"); json_writer_string(writer, state->entries[i].name);
        json_writer_key(writer, "instances");    registry_write_instances(writer, state, state->entries[i].name);
        json_writer_end_object(writer);
    }
    json_writer_end_array(writer);
    pthread_mutex_unlock(&state->mu);
    size_t len = 0;
    const char *jdata = json_writer_data(writer, &len);
    char *out = strdup(jdata ? jdata : "[]");
    json_writer_free(writer);
    if (!out) return PICOMESH_ERR(picomesh_json, "registry_list: out of memory");
    return PICOMESH_OK(picomesh_json, out);
}

PICOMESH_CLASS_ANNOTATE("override@registry:registry:registry_count")
struct picomesh_size_result registry_registry_count_impl(struct ctx *ctx, struct object *obj,
                                                         struct yheaders *hdrs)
{
    (void)ctx; (void)obj; (void)hdrs;
    struct registry_state *state = registry_state();
    pthread_mutex_lock(&state->mu);
    size_t count = state->count;
    pthread_mutex_unlock(&state->mu);
    return PICOMESH_OK(picomesh_size, count);
}

#include "registry.gen.c"
