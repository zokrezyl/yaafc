/* exec — sharded key-affine blocking executor. See exec.h.
 *
 * Each shard is one worker thread with a FIFO work queue (uv_mutex + uv_cond).
 * A coroutine submits work for key K: it appends a (stack-allocated) work item
 * to shard K%N's queue and yields. The shard thread runs fn(shard_state, arg)
 * then wakes the coroutine on its loop via loop_post_resume (coroutines are
 * thread-confined, so the resume must happen on the originating loop thread).
 *
 * The work item lives on the parked coroutine's stack: valid because the
 * coroutine cannot resume until the shard posts the resume, and the shard does
 * not touch the item after reading coro/loop to post it. */

#include <picomesh/loop/exec.h>
#include <picomesh/loop/loop.h>
#include <picomesh/picoco/coro.h>

#include <uv.h>
#include <stdlib.h>

struct exec_work {
    void (*fn)(void *shard_state, void *arg);
    void *arg;
    struct loop *loop;
    struct picomesh_coro *coro;
    struct exec_work *next;
};

struct exec_shard {
    uv_thread_t thread;
    uv_mutex_t mu;
    uv_cond_t cv;
    struct exec_work *head, *tail;
    void *state;
    struct exec *owner;
};

struct exec {
    int n;
    struct exec_shard *shards;
    void *(*shard_init)(void *ud);
    void (*shard_free)(void *state);
    void *ud;
};

static void shard_main(void *arg)
{
    struct exec_shard *s = arg;
    s->state = s->owner->shard_init ? s->owner->shard_init(s->owner->ud) : NULL;
    for (;;) {
        uv_mutex_lock(&s->mu);
        while (!s->head)
            uv_cond_wait(&s->cv, &s->mu);
        struct exec_work *w = s->head;
        s->head = w->next;
        if (!s->head) s->tail = NULL;
        uv_mutex_unlock(&s->mu);

        w->fn(s->state, w->arg);
        /* Read coro/loop, then post. After this we never touch `w` again —
         * the resume may free its (stack) storage out from under us. */
        loop_post_resume(w->loop, w->coro);
    }
    /* not reached: process-lifetime worker (matches sharded_storage's shard set) */
}

struct exec *exec_create(int n, void *(*shard_init)(void *), void (*shard_free)(void *), void *ud)
{
    if (n < 1) n = 1;
    struct exec *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->shards = calloc((size_t)n, sizeof(struct exec_shard));
    if (!e->shards) { free(e); return NULL; }
    e->n = n;
    e->shard_init = shard_init;
    e->shard_free = shard_free;
    e->ud = ud;
    for (int i = 0; i < n; i++) {
        struct exec_shard *s = &e->shards[i];
        s->owner = e;
        uv_mutex_init(&s->mu);
        uv_cond_init(&s->cv);
        if (uv_thread_create(&s->thread, shard_main, s) != 0) {
            uv_cond_destroy(&s->cv);
            uv_mutex_destroy(&s->mu);
            if (i == 0) {
                /* NO worker thread could start — e.g. a wasm build with no
                 * pthreads. Return NULL so callers fall back to running work
                 * INLINE (exec_submit's no-executor path). Leaving e->n=1
                 * here would point submit at shard 0, whose mutex/cond we just
                 * destroyed, so the next exec_submit would lock a destroyed
                 * mutex — hang or crash. This is the single-threaded-runtime
                 * safety net. */
                free(e->shards);
                free(e);
                return NULL;
            }
            /* Some shards started: keep exactly those (correct, just less
             * parallel); submit routes by key % e->n. */
            e->n = i;
            break;
        }
    }
    return e;
}

void exec_submit(struct exec *e, struct loop *loop, uint32_t key,
                  void (*fn)(void *shard_state, void *arg), void *arg)
{
    struct picomesh_coro *self = e ? picomesh_coro_current() : NULL;
    if (!e || !loop || !self) {
        /* No executor or not in a coroutine — run inline on a throwaway state,
         * preserving the previous run-blocking-or-inline fallback behaviour. */
        void *st = (e && e->shard_init) ? e->shard_init(e->ud) : NULL;
        fn(st, arg);
        if (e && e->shard_free) e->shard_free(st);
        return;
    }
    struct exec_shard *s = &e->shards[key % (uint32_t)e->n];
    struct exec_work w = {.fn = fn, .arg = arg, .loop = loop, .coro = self, .next = NULL};
    uv_mutex_lock(&s->mu);
    if (s->tail) s->tail->next = &w;
    else s->head = &w;
    s->tail = &w;
    uv_cond_signal(&s->cv);
    uv_mutex_unlock(&s->mu);
    picomesh_coro_yield(); /* parked until the shard finishes and posts our resume */
}

int exec_shard_count(const struct exec *e)
{
    return e ? e->n : 0;
}
