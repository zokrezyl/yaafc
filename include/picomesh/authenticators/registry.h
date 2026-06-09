/* Authenticator registry — build an ordered chain from config and run it.
 *
 * The frontend reads `authenticators:` (a list of typed dicts) and asks this
 * module to materialise a chain; each entry's `type:` selects a registered
 * authenticator. `run` walks the chain: first match wins; a matched-but-invalid
 * credential short-circuits (no downgrade). Adding a new authenticator type =
 * one new module + one line in the registry; the pipeline is untouched. */

#ifndef PICOMESH_AUTHENTICATORS_REGISTRY_H
#define PICOMESH_AUTHENTICATORS_REGISTRY_H

#include <picomesh/authenticators/base.h>
#include <picomesh/core/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct picomesh_authn_chain;

/* Build the chain from the `authenticators:` list config node (each entry a
 * map with a `type:` key plus type-specific config). An unknown type fails
 * loud — security config is never silently dropped. A NULL/empty list builds
 * an empty chain (every request is anonymous). */
struct picomesh_void_ptr_result
picomesh_authn_chain_build(struct picomesh_engine *engine, const struct config_node *list);

/* Run the chain over `request`. OK carries the outcome (whose owned
 * `jwt`/`error` are freed with picomesh_authn_outcome_free); ERR carries an
 * infrastructure failure's cause chain (an authenticator's downstream broke). */
struct picomesh_authn_outcome_result
picomesh_authn_chain_run(struct picomesh_authn_chain *chain,
                         const struct picomesh_authn_request *request);

void picomesh_authn_chain_free(struct picomesh_authn_chain *chain);
void picomesh_authn_outcome_free(struct picomesh_authn_outcome *outcome);

#ifdef __cplusplus
}
#endif

#endif /* PICOMESH_AUTHENTICATORS_REGISTRY_H */
