/* jinvoke — JSON-aware method invocation registry.
 *
 * Linked list of per-module lookup hooks; the first non-NULL hit
 * wins. Each codegen-emitted module registers its own hook via
 * `jinvoke_add_lookup` from a __attribute__((constructor)). */

#include <picomesh/picoclass/jinvoke.h>

#include <stdlib.h>

struct lookup_node {
    jinvoke_lookup_fn fn;
    struct lookup_node *next;
};

static struct lookup_node **chain_head(void)
{
    static struct lookup_node *head = NULL;
    return &head;
}

void jinvoke_add_lookup(jinvoke_lookup_fn fn)
{
    if (!fn) return;
    struct lookup_node *node = calloc(1, sizeof(*node));
    if (!node) return;
    struct lookup_node **head = chain_head();
    node->fn = fn;
    node->next = *head;
    *head = node;
}

jinvoke_fn jinvoke_for(const char *qname)
{
    if (!qname) return NULL;
    for (struct lookup_node *node = *chain_head(); node; node = node->next) {
        jinvoke_fn found_fn = node->fn(qname);
        if (found_fn) return found_fn;
    }
    return NULL;
}

/* ---- param-signature reflection: a parallel lookup chain ------------- */

struct params_node {
    jinvoke_params_lookup_fn fn;
    struct params_node *next;
};

static struct params_node **params_chain_head(void)
{
    static struct params_node *head = NULL;
    return &head;
}

void jinvoke_params_add_lookup(jinvoke_params_lookup_fn fn)
{
    if (!fn) return;
    struct params_node *node = calloc(1, sizeof(*node));
    if (!node) return;
    struct params_node **head = params_chain_head();
    node->fn = fn;
    node->next = *head;
    *head = node;
}

const struct jinvoke_params *jinvoke_params_for(const char *qname)
{
    if (!qname) return NULL;
    for (struct params_node *node = *params_chain_head(); node; node = node->next) {
        const struct jinvoke_params *params = node->fn(qname);
        if (params) return params;
    }
    return NULL;
}
