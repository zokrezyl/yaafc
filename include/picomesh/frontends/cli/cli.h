/* cli — one-shot local invoker.
 *
 * The CLI is *not* a server. It's a subcommand of the driver:
 *
 *     picomesh invoke <plugin>_<class>_<method> [arg1 arg2 ...]
 *
 * It creates a fresh local instance of the named class, parses
 * positional args as JSON scalars (numbers / true|false|null /
 * otherwise treated as strings), calls the local jinvoke, and prints
 * the JSON-encoded result on stdout.
 *
 * State is not persisted between invocations — analogous to yaapp's
 * `cli` runner which spins up a fresh proxy tree per call. For
 * persistent state, point a `picomesh invoke` at a running `picomesh yttp`
 * server through its JSON-RPC interface (separate feature). */

#ifndef PICOMESH_FRONTENDS_CLI_CLI_H
#define PICOMESH_FRONTENDS_CLI_CLI_H

#include <picomesh/core/result.h>

struct picomesh_engine;

/* Dispatch the parsed CLI subcommand. The OK value is the process exit code:
 *   0  — success
 *   2  — usage error
 *   1  — invoke / lookup error
 * ERR carries an infrastructure failure's cause chain.
 *
 * Reads the subcommand + remaining argv from the engine's stored CLI
 * chain. */
struct picomesh_int_result picomesh_cli_dispatch(struct picomesh_engine *e);

#endif /* PICOMESH_FRONTENDS_CLI_CLI_H */
