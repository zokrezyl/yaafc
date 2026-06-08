/* minvoke — MessagePack method invocation registry.
 *
 * Linked list of per-module lookup hooks; the first non-NULL hit wins. Each
 * codegen-emitted module registers its hook via `minvoke_add_lookup` from its
 * activation entry point, exactly like the jinvoke chain. */

#include <picomesh/yclass/minvoke.h>

#include <stdlib.h>

struct lookup_node {
    minvoke_lookup_fn fn;
    struct lookup_node *next;
};

static struct lookup_node **chain_head(void)
{
    static struct lookup_node *head = NULL;
    return &head;
}

void minvoke_add_lookup(minvoke_lookup_fn fn)
{
    if (!fn)
        return;
    struct lookup_node *node = calloc(1, sizeof(*node));
    if (!node)
        return;
    struct lookup_node **head = chain_head();
    node->fn = fn;
    node->next = *head;
    *head = node;
}

minvoke_fn minvoke_for(const char *qname)
{
    if (!qname)
        return NULL;
    for (struct lookup_node *node = *chain_head(); node; node = node->next) {
        minvoke_fn found_fn = node->fn(qname);
        if (found_fn)
            return found_fn;
    }
    return NULL;
}
