/* Class / object runtime — per-domain slot tables.
 *
 * One slot_table per domain (allocated lazily on slot_table_get).
 * method_slot values pack the domain id in bits 27..24 and the
 * per-domain local index in bits 23..0, so two domains can share local
 * names without colliding. The global class_registry stays keyed by the
 * qualified class name; classes carry per-domain dispatch slices,
 * indexed by the same domain id encoded in method_slot.
 *
 *     bits 31..28   reserved (always 0 for valid slots; UINT32_MAX
 *                   is the sentinel METHOD_SLOT_UNDEFINED).
 *     bits 27..24   domain_id (1..15; 0 is invalid / never returned).
 *     bits 23..0    local index inside that domain's slot_table.
 *
 * Every fallible runtime entry point returns a Result type from
 * <picomesh/core/result.h>. Callers check PICOMESH_IS_ERR and either
 * propagate (PICOMESH_RETURN_IF_ERR) or absorb at a boundary. */

#ifndef PICOMESH_PICOCLASS_CLASS_H
#define PICOMESH_PICOCLASS_CLASS_H

#include <picomesh/core/result.h>

#include <stddef.h>
#include <stdint.h>

/* Plugin sources tag data structs and impl functions with
 *   PICOMESH_CLASS_ANNOTATE("class@DOMAIN:CLASS")
 *   PICOMESH_CLASS_ANNOTATE("override@DOMAIN:CLASS:SLOT")
 *
 * The codegen (src/picomesh/picoclass/gen/codegen.py) reads the literal
 * `[[clang::annotate(...)]]` form via clang's `-fsyntax-only` AST dump.
 * At normal compile time the attribute is just dropped — clang ignores
 * `[[clang::annotate]]` for the actual compilation.
 *
 * gcc 13/14 only parses `[[ns::name]]` scoped attributes for the
 * standardized namespaces (`gnu`, `_Noreturn`, …); `clang::annotate`
 * is unknown to gcc and produces "expected ']' before ':'". Gate the
 * attribute behind `__clang__` so cross-compiles with gcc (linux-riscv64,
 * etc.) skip it entirely. The codegen still sees the literal text
 * because it invokes clang directly. */
#ifdef __clang__
#define PICOMESH_CLASS_ANNOTATE(x) [[clang::annotate(x)]]
#else
#define PICOMESH_CLASS_ANNOTATE(x)
#endif

struct object;
struct class;
struct slot_table;
struct peer_channel;
struct yheaders; /* request-header bag — 3rd arg of every method; see yheaders.h
                  */

/* Framework dispatch context — nothing more. `session` is local-only:
 * NULL → dispatch the call in-process; non-NULL → forward it to that
 * remote. Request metadata (uid, sid, trace_id, anything) does NOT live
 * here — it travels as an explicit `struct yheaders *` argument
 * (the 3rd parameter of every method), serialized by the framework, not
 * mixed into this struct. See <picomesh/picoclass/yheaders.h>. */
struct ctx {
  struct peer_channel *peer; /* NULL → local; set → remote */
  /* For the local path (peer==NULL): the address of the owning worker's
   * in-process default-instance cache head. It lets rpc_object_acquire
   * reuse ONE instance per class across calls — so a collocated service
   * that keeps state in memory (e.g. git_repo's repo table) survives
   * between requests, mirroring how the remote path caches one proxy per
   * (peer, class). NULL ⇒ no cache available, allocate a fresh instance
   * (CLI / unit-test callers). Opaque here; rpc.c owns the element type. */
  void **local_cache;
};

enum class_type {
  CLASS_TYPE_REGULAR = 0,
  CLASS_TYPE_MIXIN = 1,
};

typedef void (*method_id_t)(void);
typedef void (*impl_t)(void);
typedef uint32_t method_slot;
#define METHOD_SLOT_UNDEFINED UINT32_MAX

#define METHOD_SLOT_DOMAIN_BITS 4
#define METHOD_SLOT_MAX_DOMAINS (1u << METHOD_SLOT_DOMAIN_BITS)
#define METHOD_SLOT_INDEX_SHIFT 24
#define METHOD_SLOT_INDEX_MASK ((1u << METHOD_SLOT_INDEX_SHIFT) - 1)
#define METHOD_SLOT_DOMAIN_OF(s) (((s) >> METHOD_SLOT_INDEX_SHIFT) & 0xFu)
#define METHOD_SLOT_INDEX_OF(s) ((s) & METHOD_SLOT_INDEX_MASK)
#define METHOD_SLOT_PACK(dom, idx)                                             \
  (((uint32_t)(dom) << METHOD_SLOT_INDEX_SHIFT) |                              \
   ((idx) & METHOD_SLOT_INDEX_MASK))

struct class_descriptor {
  const char *name; /* qualified, e.g. "ystorage_kv" */
  enum class_type type;
  size_t data_size;
};

struct op {
  const char *slot_domain; /* slot's owning module */
  const char *name;        /* slot local name */
  method_id_t method_id;   /* the public-stub fn ptr (vtable key) */
  impl_t impl;             /* override for this slot */
};

struct object {
  const struct class *klass;
};

struct picomesh_str {
  char buf[128];
};

/* --- Result types ------------------------------------------------- */
PICOMESH_RESULT_DECLARE(slot_table_ptr, struct slot_table *);
PICOMESH_RESULT_DECLARE(class_ptr, const struct class *);
PICOMESH_RESULT_DECLARE(object_ptr, struct object *);
PICOMESH_RESULT_DECLARE(method_slot, method_slot);
PICOMESH_RESULT_DECLARE(impl, impl_t);
PICOMESH_RESULT_DECLARE(const_char_ptr, const char *);
PICOMESH_RESULT_DECLARE(picomesh_str, struct picomesh_str);

/* --- Per-domain slot_table ---------------------------------------- */

struct slot_table_ptr_result slot_table_get(const char *domain);

/* --- Registration (one-shot, at class_register) ------------------- */
struct method_slot_result
method_slot_register(const char *domain, const char *name, method_id_t id);

/* --- Lookups (per call / per handshake) --------------------------- */
struct method_slot_result method_slot_get(const char *domain, method_id_t id);
struct method_slot_result method_slot_by_name(const char *domain,
                                              const char *name);
struct method_slot_result method_slot_by_qname(const char *qname);
struct const_char_ptr_result method_slot_name(method_slot slot);

/* --- Dispatch / registry ------------------------------------------ */
impl_t class_dispatch_lookup(const struct class *cls, method_slot slot);
const struct class *object_class(const struct object *obj);

struct class_ptr_result class_register(const struct class_descriptor *desc,
                                       const struct op *ops, size_t ops_count,
                                       const struct class *parent,
                                       const struct class *const *mixins,
                                       size_t mixin_count);

struct class_ptr_result class_by_name(const char *name);

typedef struct class_ptr_result (*accessor_lookup_fn)(const char *name);
struct picomesh_void_result class_add_accessor_lookup(accessor_lookup_fn fn);

struct picomesh_void_result
class_for_each_slot(const struct class *cls,
                    void (*cb)(const char *name, method_slot slot, void *ud),
                    void *userdata);

/* Enumerate every class in the registry. The callback gets each class and
 * its qualified name. Registration is activation in this runtime (a process
 * only registers the classes it activates locally + the ones it proxies as
 * remotes), so this enumerates exactly the ACTIVE service classes — it is
 * NOT a window onto every compiled-in plugin. Used to enrich the /_describe
 * service tree with each active service's classes/methods. */
void class_for_each(void (*cb)(const struct class *cls, const char *name,
                               void *ud),
                    void *userdata);

struct object_ptr_result object_alloc(const struct class *cls);
void object_free(struct object *obj);

#endif /* PICOMESH_PICOCLASS_CLASS_H */
