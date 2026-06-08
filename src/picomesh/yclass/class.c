/* Class runtime — per-domain slot tables.
 *
 * Ported from yetty PoC class-object-model. The design's docstring lives in
 * include/picomesh/yclass/class.h. */

#include <picomesh/yclass/class.h>
#include <picomesh/ycore/result.h>
#include <picomesh/ycore/ytrace.h>

#include <uthash.h>

#include <stdlib.h>
#include <string.h>

struct class {
    const struct class_descriptor *desc;
    const struct class *parent;
    const struct class **mixins;
    size_t mixin_count;

    /* Per-domain dispatch. dispatch_by_domain[d].impls is a flat array
     * indexed by local_idx (i.e. METHOD_SLOT_INDEX_OF(slot)). count is
     * the array length. Domains the class never touches stay at
     * count=0 / impls=NULL. */
    struct dispatch_slice {
        impl_t *impls;
        size_t count;
    } dispatch_by_domain[METHOD_SLOT_MAX_DOMAINS];

    size_t instance_size;
    UT_hash_handle hh; /* keyed by desc->name */
};

struct slot_entry {
    char *qname;            /* owned: "<domain>_<local_name>" */
    const char *local_name; /* points into qname after the boundary */
    method_id_t local_id;
    method_slot slot_index;
    UT_hash_handle hh_lname;
    UT_hash_handle hh_id;
    UT_hash_handle hh_qname;
};

struct slot_table {
    char *domain;
    uint8_t domain_id;
    struct slot_entry **by_index;
    size_t count;
    size_t cap;
    struct slot_entry *by_local_name;
    struct slot_entry *by_local_id;
    UT_hash_handle hh_dom;
};

struct domain_registry {
    struct slot_table *by_id[METHOD_SLOT_MAX_DOMAINS];
    struct slot_table *by_name;
    uint8_t next_id;
};

static struct domain_registry *dreg(void)
{
    static struct domain_registry registry = {0};
    if (registry.next_id == 0) {
        registry.next_id = 1; /* domain_id 0 reserved as invalid */
    }
    return &registry;
}

static struct slot_entry **global_qname_root(void)
{
    static struct slot_entry *root = NULL;
    return &root;
}

struct slot_table_ptr_result slot_table_get(const char *domain)
{
    ydebug("domain=%s", domain ? domain : "(null)");
    if (!domain) return PICOMESH_ERR(slot_table_ptr, "slot_table_get: NULL domain");
    struct domain_registry *reg = dreg();
    struct slot_table *table = NULL;
    HASH_FIND(hh_dom, reg->by_name, domain, strlen(domain), table);
    if (table) return PICOMESH_OK(slot_table_ptr, table);
    if (reg->next_id >= METHOD_SLOT_MAX_DOMAINS) {
        return PICOMESH_ERR(slot_table_ptr, "slot_table_get: domain id capacity exhausted");
    }
    table = calloc(1, sizeof(*table));
    if (!table) return PICOMESH_ERR(slot_table_ptr, "slot_table_get: calloc(slot_table) failed");
    table->domain = strdup(domain);
    if (!table->domain) {
        free(table);
        return PICOMESH_ERR(slot_table_ptr, "slot_table_get: strdup(domain) failed");
    }
    table->domain_id = reg->next_id++;
    reg->by_id[table->domain_id] = table;
    HASH_ADD_KEYPTR(hh_dom, reg->by_name, table->domain, strlen(table->domain), table);
    return PICOMESH_OK(slot_table_ptr, table);
}

struct method_slot_result method_slot_register(const char *domain, const char *name,
                                               method_id_t id)
{
    ydebug("domain=%s name=%s id=%p", domain ? domain : "(null)", name ? name : "(null)",
           (void *)id);
    if (!domain || !name) {
        return PICOMESH_ERR(method_slot, "method_slot_register: NULL domain or name");
    }
    struct slot_table_ptr_result table_res = slot_table_get(domain);
    PICOMESH_RETURN_IF_ERR(method_slot, table_res, "method_slot_register: slot_table_get failed");
    struct slot_table *table = table_res.value;

    struct slot_entry *entry = NULL;
    HASH_FIND(hh_lname, table->by_local_name, name, strlen(name), entry);
    if (entry) return PICOMESH_OK(method_slot, entry->slot_index);

    entry = calloc(1, sizeof(*entry));
    if (!entry) return PICOMESH_ERR(method_slot, "method_slot_register: calloc(slot_entry) failed");

    size_t dom_len = strlen(table->domain);
    size_t loc_len = strlen(name);
    entry->qname = malloc(dom_len + 1 + loc_len + 1);
    if (!entry->qname) {
        free(entry);
        return PICOMESH_ERR(method_slot, "method_slot_register: malloc(qname) failed");
    }
    memcpy(entry->qname, table->domain, dom_len);
    entry->qname[dom_len] = '_';
    memcpy(entry->qname + dom_len + 1, name, loc_len + 1);
    entry->local_name = entry->qname + dom_len + 1;
    entry->local_id = id;

    if (table->count >= table->cap) {
        size_t ncap = table->cap ? table->cap * 2 : 32;
        while (ncap <= table->count) ncap *= 2;
        struct slot_entry **new_index = realloc(table->by_index, ncap * sizeof(*new_index));
        if (!new_index) {
            free(entry->qname);
            free(entry);
            return PICOMESH_ERR(method_slot, "method_slot_register: realloc(by_index) failed");
        }
        memset(new_index + table->cap, 0, (ncap - table->cap) * sizeof(*new_index));
        table->by_index = new_index;
        table->cap = ncap;
    }
    entry->slot_index = METHOD_SLOT_PACK(table->domain_id, table->count);
    table->by_index[table->count++] = entry;

    HASH_ADD_KEYPTR(hh_lname, table->by_local_name, entry->local_name, strlen(entry->local_name), entry);
    HASH_ADD(hh_id, table->by_local_id, local_id, sizeof(method_id_t), entry);
    HASH_ADD_KEYPTR(hh_qname, *global_qname_root(), entry->qname, strlen(entry->qname), entry);
    return PICOMESH_OK(method_slot, entry->slot_index);
}

struct method_slot_result method_slot_get(const char *domain, method_id_t id)
{
    ydebug("domain=%s id=%p", domain ? domain : "(null)", (void *)id);
    if (!domain || !id) return PICOMESH_ERR(method_slot, "method_slot_get: NULL domain or id");
    struct domain_registry *reg = dreg();
    struct slot_table *table = NULL;
    HASH_FIND(hh_dom, reg->by_name, domain, strlen(domain), table);
    if (!table) return PICOMESH_ERR(method_slot, "method_slot_get: domain not registered");
    struct slot_entry *entry = NULL;
    HASH_FIND(hh_id, table->by_local_id, &id, sizeof(method_id_t), entry);
    if (!entry) return PICOMESH_ERR(method_slot, "method_slot_get: id not in domain's slot table");
    return PICOMESH_OK(method_slot, entry->slot_index);
}

struct method_slot_result method_slot_by_name(const char *domain, const char *name)
{
    ydebug("domain=%s name=%s", domain ? domain : "(null)", name ? name : "(null)");
    if (!domain || !name) {
        return PICOMESH_ERR(method_slot, "method_slot_by_name: NULL domain or name");
    }
    struct domain_registry *reg = dreg();
    struct slot_table *table = NULL;
    HASH_FIND(hh_dom, reg->by_name, domain, strlen(domain), table);
    if (!table) return PICOMESH_ERR(method_slot, "method_slot_by_name: domain not registered");
    struct slot_entry *entry = NULL;
    HASH_FIND(hh_lname, table->by_local_name, name, strlen(name), entry);
    if (!entry) return PICOMESH_ERR(method_slot, "method_slot_by_name: name not in slot table");
    return PICOMESH_OK(method_slot, entry->slot_index);
}

struct method_slot_result method_slot_by_qname(const char *qname)
{
    ydebug("qname=%s", qname ? qname : "(null)");
    if (!qname) return PICOMESH_ERR(method_slot, "method_slot_by_qname: NULL qname");
    struct slot_entry *entry = NULL;
    HASH_FIND(hh_qname, *global_qname_root(), qname, strlen(qname), entry);
    if (!entry) return PICOMESH_ERR(method_slot, "method_slot_by_qname: qname not registered");
    return PICOMESH_OK(method_slot, entry->slot_index);
}

struct const_char_ptr_result method_slot_name(method_slot slot)
{
    ydebug("slot=0x%08x", slot);
    if (slot == METHOD_SLOT_UNDEFINED) {
        return PICOMESH_ERR(const_char_ptr, "method_slot_name: METHOD_SLOT_UNDEFINED");
    }
    uint8_t domain_id = METHOD_SLOT_DOMAIN_OF(slot);
    uint32_t index = METHOD_SLOT_INDEX_OF(slot);
    if (domain_id == 0 || domain_id >= METHOD_SLOT_MAX_DOMAINS) {
        return PICOMESH_ERR(const_char_ptr, "method_slot_name: invalid domain id");
    }
    struct slot_table *table = dreg()->by_id[domain_id];
    if (!table || index >= table->count) {
        return PICOMESH_ERR(const_char_ptr, "method_slot_name: slot index out of range");
    }
    return PICOMESH_OK(const_char_ptr, table->by_index[index]->qname);
}

impl_t class_dispatch_lookup(const struct class *cls, method_slot slot)
{
    /* Not Result — "this class does not override this slot" is a
     * normal flow, not an error. Returns NULL to indicate that. */
    if (!cls || slot == METHOD_SLOT_UNDEFINED) return NULL;
    uint8_t domain_id = METHOD_SLOT_DOMAIN_OF(slot);
    uint32_t index = METHOD_SLOT_INDEX_OF(slot);
    if (domain_id == 0 || domain_id >= METHOD_SLOT_MAX_DOMAINS) return NULL;
    const struct dispatch_slice *slice = &cls->dispatch_by_domain[domain_id];
    if (index >= slice->count) return NULL;
    return slice->impls[index];
}

const struct class *object_class(const struct object *obj)
{
    return obj ? obj->klass : NULL;
}

/* --- class_registry ----------------------------------------------- */

struct class_registry {
    struct class **by_index;
    size_t count;
    size_t cap;
    struct class *by_name;
};

static struct class_registry *class_registry_get(void)
{
    static struct class_registry reg = {0};
    return &reg;
}

static struct picomesh_void_result class_registry_add(struct class *cls)
{
    ydebug("cls=%s", cls && cls->desc ? cls->desc->name : "(null)");
    struct class_registry *reg = class_registry_get();
    if (reg->count == reg->cap) {
        size_t ncap = reg->cap ? reg->cap * 2 : 16;
        struct class **new_index = realloc(reg->by_index, ncap * sizeof(*new_index));
        if (!new_index) return PICOMESH_ERR(picomesh_void, "class_registry_add: realloc failed");
        reg->by_index = new_index;
        reg->cap = ncap;
    }
    reg->by_index[reg->count++] = cls;
    HASH_ADD_KEYPTR(hh, reg->by_name, cls->desc->name, strlen(cls->desc->name), cls);
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result dispatch_slice_grow(struct dispatch_slice *slice, size_t needed)
{
    if (needed <= slice->count) return PICOMESH_OK_VOID();
    impl_t *new_impls = realloc(slice->impls, needed * sizeof(*new_impls));
    if (!new_impls) return PICOMESH_ERR(picomesh_void, "dispatch_slice_grow: realloc failed");
    memset(new_impls + slice->count, 0, (needed - slice->count) * sizeof(*new_impls));
    slice->impls = new_impls;
    slice->count = needed;
    return PICOMESH_OK_VOID();
}

static struct picomesh_void_result class_inherit_dispatch(struct class *cls, const struct class *src)
{
    for (uint8_t domain_id = 0; domain_id < METHOD_SLOT_MAX_DOMAINS; ++domain_id) {
        const struct dispatch_slice *src_slice = &src->dispatch_by_domain[domain_id];
        if (src_slice->count == 0) continue;
        struct dispatch_slice *dest_slice = &cls->dispatch_by_domain[domain_id];
        struct picomesh_void_result grow_res = dispatch_slice_grow(dest_slice, src_slice->count);
        PICOMESH_RETURN_IF_ERR(picomesh_void, grow_res, "class_inherit_dispatch: grow failed");
        for (size_t i = 0; i < src_slice->count; ++i) {
            if (src_slice->impls[i]) dest_slice->impls[i] = src_slice->impls[i];
        }
    }
    return PICOMESH_OK_VOID();
}

static void class_destroy(struct class *cls)
{
    if (!cls) return;
    for (uint8_t domain_id = 0; domain_id < METHOD_SLOT_MAX_DOMAINS; ++domain_id) free(cls->dispatch_by_domain[domain_id].impls);
    free((void *)cls->mixins);
    free(cls);
}

struct class_ptr_result class_register(const struct class_descriptor *desc, const struct op *ops,
                                       size_t ops_count, const struct class *parent,
                                       const struct class *const *mixins, size_t mixin_count)
{
    ydebug("class=%s ops=%zu parent=%s mixins=%zu",
           desc && desc->name ? desc->name : "(null)", ops_count,
           parent && parent->desc ? parent->desc->name : "(none)", mixin_count);
    if (!desc) return PICOMESH_ERR(class_ptr, "class_register: NULL descriptor");
    struct class *cls = calloc(1, sizeof(*cls));
    if (!cls) return PICOMESH_ERR(class_ptr, "class_register: calloc(class) failed");
    cls->desc = desc;
    cls->parent = parent;

    if (mixin_count > 0) {
        cls->mixins = malloc(mixin_count * sizeof(*cls->mixins));
        if (!cls->mixins) {
            class_destroy(cls);
            return PICOMESH_ERR(class_ptr, "class_register: malloc(mixins) failed");
        }
        memcpy((void *)cls->mixins, mixins, mixin_count * sizeof(*cls->mixins));
        cls->mixin_count = mixin_count;
    }

    size_t offset = sizeof(struct object);
    for (const struct class *ancestor = parent; ancestor != NULL; ancestor = ancestor->parent) {
        offset += ancestor->desc->data_size;
        for (size_t m = 0; m < ancestor->mixin_count; ++m) {
            offset += ancestor->mixins[m]->desc->data_size;
        }
    }
    offset += desc->data_size;
    for (size_t m = 0; m < mixin_count; ++m) offset += mixins[m]->desc->data_size;
    cls->instance_size = offset;

    if (parent) {
        struct picomesh_void_result inherit_res = class_inherit_dispatch(cls, parent);
        if (PICOMESH_IS_ERR(inherit_res)) {
            class_destroy(cls);
            return PICOMESH_ERR(class_ptr, "class_register: inherit parent dispatch failed", inherit_res);
        }
    }
    for (size_t m = 0; m < mixin_count; ++m) {
        struct picomesh_void_result inherit_res = class_inherit_dispatch(cls, mixins[m]);
        if (PICOMESH_IS_ERR(inherit_res)) {
            class_destroy(cls);
            return PICOMESH_ERR(class_ptr, "class_register: inherit mixin dispatch failed", inherit_res);
        }
    }

    for (size_t i = 0; i < ops_count; ++i) {
        struct method_slot_result slot_res =
            method_slot_register(ops[i].slot_domain, ops[i].name, ops[i].method_id);
        if (PICOMESH_IS_ERR(slot_res)) {
            class_destroy(cls);
            return PICOMESH_ERR(class_ptr, "class_register: method_slot_register failed", slot_res);
        }
        method_slot slot = slot_res.value;
        uint8_t domain_id = METHOD_SLOT_DOMAIN_OF(slot);
        uint32_t index = METHOD_SLOT_INDEX_OF(slot);
        struct dispatch_slice *slice = &cls->dispatch_by_domain[domain_id];
        struct picomesh_void_result grow_res = dispatch_slice_grow(slice, (size_t)index + 1);
        if (PICOMESH_IS_ERR(grow_res)) {
            class_destroy(cls);
            return PICOMESH_ERR(class_ptr, "class_register: dispatch slice grow failed", grow_res);
        }
        slice->impls[index] = ops[i].impl;
    }

    struct picomesh_void_result add_res = class_registry_add(cls);
    if (PICOMESH_IS_ERR(add_res)) {
        class_destroy(cls);
        return PICOMESH_ERR(class_ptr, "class_register: class_registry_add failed", add_res);
    }
    return PICOMESH_OK(class_ptr, cls);
}

/* --- accessor chain ----------------------------------------------- */

struct accessor_node {
    accessor_lookup_fn fn;
    struct accessor_node *next;
};

static struct accessor_node **accessor_chain_head(void)
{
    static struct accessor_node *head = NULL;
    return &head;
}

struct picomesh_void_result class_add_accessor_lookup(accessor_lookup_fn fn)
{
    ydebug("fn=%p", (void *)(uintptr_t)fn);
    if (!fn) return PICOMESH_ERR(picomesh_void, "class_add_accessor_lookup: NULL fn");
    struct accessor_node *node = calloc(1, sizeof(*node));
    if (!node) return PICOMESH_ERR(picomesh_void, "class_add_accessor_lookup: calloc failed");
    node->fn = fn;
    struct accessor_node **head = accessor_chain_head();
    node->next = *head;
    *head = node;
    return PICOMESH_OK_VOID();
}

struct class_ptr_result class_by_name(const char *name)
{
    ydebug("name=%s", name ? name : "(null)");
    if (!name) return PICOMESH_ERR(class_ptr, "class_by_name: NULL name");
    struct class_registry *reg = class_registry_get();
    struct class *cls = NULL;
    HASH_FIND_STR(reg->by_name, name, cls);
    if (cls) return PICOMESH_OK(class_ptr, cls);
    for (struct accessor_node *node = *accessor_chain_head(); node; node = node->next) {
        struct class_ptr_result accessor_res = node->fn(name);
        if (PICOMESH_IS_ERR(accessor_res)) {
            return PICOMESH_ERR(class_ptr, "class_by_name: accessor hook failed", accessor_res);
        }
        if (accessor_res.value) return accessor_res;
    }
    return PICOMESH_ERR(class_ptr, "class_by_name: class not found in registry or any hook");
}

void class_for_each_slot(const struct class *cls,
                         void (*cb)(const char *name, method_slot slot, void *ud), void *userdata)
{
    ydebug("cls=%s", cls && cls->desc ? cls->desc->name : "(null)");
    if (!cls || !cb) return;
    for (uint8_t domain_id = 0; domain_id < METHOD_SLOT_MAX_DOMAINS; ++domain_id) {
        const struct dispatch_slice *slice = &cls->dispatch_by_domain[domain_id];
        for (size_t i = 0; i < slice->count; ++i) {
            if (!slice->impls[i]) continue;
            method_slot slot = METHOD_SLOT_PACK(domain_id, i);
            struct const_char_ptr_result name_res = method_slot_name(slot);
            if (PICOMESH_IS_OK(name_res) && name_res.value) {
                cb(name_res.value, slot, userdata);
            } else if (PICOMESH_IS_ERR(name_res)) {
                picomesh_error_destroy(name_res.error);
            }
        }
    }
}

void class_for_each(void (*cb)(const struct class *cls, const char *name, void *ud),
                    void *userdata)
{
    if (!cb) return;
    struct class_registry *reg = class_registry_get();
    for (size_t i = 0; i < reg->count; ++i) {
        struct class *cls = reg->by_index[i];
        if (cls && cls->desc && cls->desc->name) cb(cls, cls->desc->name, userdata);
    }
}

struct object_ptr_result object_alloc(const struct class *cls)
{
    ydebug("cls=%s size=%zu", cls && cls->desc ? cls->desc->name : "(null)",
           cls ? cls->instance_size : 0);
    if (!cls) return PICOMESH_ERR(object_ptr, "object_alloc: NULL class");
    struct object *obj = calloc(1, cls->instance_size);
    if (!obj) return PICOMESH_ERR(object_ptr, "object_alloc: calloc failed");
    obj->klass = cls;
    return PICOMESH_OK(object_ptr, obj);
}

void object_free(struct object *obj)
{
    ydebug("obj=%p", (void *)obj);
    free(obj);
}
