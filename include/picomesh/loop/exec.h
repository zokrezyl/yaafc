#ifndef PICOMESH_LOOP_EXEC_H
#define PICOMESH_LOOP_EXEC_H

#include <stdint.h>

/* exec — a sharded, KEY-AFFINE blocking executor.
 *
 * N worker THREADS. Work submitted with key K always runs on thread (K % N),
 * so all work for a given key is serialized onto ONE owning thread. That lets
 * per-key state (e.g. an open libgit2 `git_repository` handle) live in exactly
 * one thread and be reused across calls with NO locking — affinity replaces the
 * mutex. Different keys land on different threads and run in parallel.
 *
 * Each shard thread gets its own `state` from `shard_init` (created ON the
 * shard thread), passed to every `fn` it runs, and released by `shard_free`.
 * Use it to hold the per-shard handle cache.
 *
 * The executor is process-lifetime (no destroy): like sharded_storage's shard
 * set, the worker threads run until the process exits. */

struct exec;
struct loop;

/* Spawn N shard worker threads. `shard_init(ud)` runs once per shard on its own
 * thread to build that shard's state; `shard_free(state)` releases it. Either
 * may be NULL. Returns NULL on allocation failure. */
struct exec *exec_create(int n_shards, void *(*shard_init)(void *ud),
                         void (*shard_free)(void *state), void *ud);

/* Run `fn(shard_state, arg)` on shard `key % n_shards`, suspending the CURRENT
 * coroutine (which must be running on `loop`) until `fn` returns; the coroutine
 * is resumed on `loop`. Called outside a coroutine (bootstrap/tests) it runs
 * `fn` inline on a throwaway shard state. */
void exec_submit(struct exec *e, struct loop *loop, uint32_t key,
                 void (*fn)(void *shard_state, void *arg), void *arg);

int exec_shard_count(const struct exec *e);

#endif /* PICOMESH_LOOP_EXEC_H */
