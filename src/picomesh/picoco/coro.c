/* picomesh/picoco/coro.c — libco-backed coroutine primitive.
 *
 * libco does the stack-switch and tracks the "active" thread; we layer
 * id/name/status/finished bookkeeping on top so callers can introspect
 * a coroutine the way the design doc describes. */

#include <picomesh/picoco/coro.h>
#include <picomesh/core/ytrace.h>

#include <libco.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Per-coroutine stack. Coroutine stacks are fixed-size heap buffers (no
 * auto-growth, no guard page). The collocated/all-in-one deployment runs the
 * WHOLE service chain (gateway → token_issuer → password_authn →
 * sharded_storage → accounts → relational, …) as nested in-process calls on a
 * SINGLE coroutine stack. That used to overflow 256 KiB because each generated
 * yrpc stub reserved a ~90 KiB frame (the 64 KiB wire response + 16 KiB arg
 * buffers were stack locals); the wire buffers now come from a per-thread pool
 * (see src/picomesh/allocator), so each stub frame is ~8 KiB and a deep chain
 * fits in 256 KiB with room to spare. */
#define DEFAULT_STACK_SIZE (256 * 1024)

struct picomesh_coro {
    cothread_t thread;
    void *arg;
    picomesh_coro_entry entry;
    cothread_t resumer; /* who to switch back to on yield */
    unsigned int id;
    char *name;
    int finished;
    int status;
};

/* The currently-running coroutine handle. The libco "active" thread is
 * the main stack when this is NULL, otherwise this object's thread.
 *
 * THREAD-LOCAL: each worker thread runs its own libuv loop and its own
 * libco scheduler, so each needs an independent "current coroutine".
 * libco itself tracks its active context per-thread (co_active_handle is
 * thread_local in every backend), so this companion bookkeeping must be
 * thread-local too — otherwise two worker threads switching coroutines
 * concurrently would clobber each other's notion of who is running. */
static struct picomesh_coro **current_slot(void)
{
    static _Thread_local struct picomesh_coro *cur = NULL;
    return &cur;
}

/* Coroutine ids are purely for tracing/introspection. They're handed out
 * across all worker threads, so the counter is atomic to stay race-free
 * (and globally unique) without a lock. */
static unsigned int next_id(void)
{
    static atomic_uint counter = 0;
    return atomic_fetch_add_explicit(&counter, 1, memory_order_relaxed) + 1;
}

static void coro_trampoline(void)
{
    /* libco entry trampoline. The currently-active coroutine is `cur`
     * — we set it just before co_switch'ing in. After entry returns
     * we mark finished, restore `cur` to NULL, and switch back to
     * whoever resumed us. Subsequent resumes are no-ops via the
     * is_finished check in picomesh_coro_resume. */
    struct picomesh_coro *self = *current_slot();
    if (!self) return;
    self->entry(self->arg);
    self->finished = 1;
    cothread_t back = self->resumer;
    *current_slot() = NULL;
    co_switch(back);
}

struct picomesh_coro_ptr_result picomesh_coro_spawn(picomesh_coro_entry entry, void *arg,
                                              size_t stack_hint, const char *name)
{
    if (!entry) return PICOMESH_ERR(picomesh_coro_ptr, "picomesh_coro_spawn: NULL entry");
    struct picomesh_coro *coro = calloc(1, sizeof(*coro));
    if (!coro) return PICOMESH_ERR(picomesh_coro_ptr, "picomesh_coro_spawn: calloc failed");
    coro->entry = entry;
    coro->arg = arg;
    coro->id = next_id();
    coro->name = name ? strdup(name) : NULL;
    size_t stack = stack_hint ? stack_hint : DEFAULT_STACK_SIZE;
    coro->thread = co_create((unsigned int)stack, coro_trampoline);
    if (!coro->thread) {
        free(coro->name);
        free(coro);
        return PICOMESH_ERR(picomesh_coro_ptr, "picomesh_coro_spawn: co_create failed");
    }
    ydebug("spawn id=%u name=%s stack=%zu thread=%p", coro->id,
           coro->name ? coro->name : "(anon)", stack, coro->thread);
    return PICOMESH_OK(picomesh_coro_ptr, coro);
}

void picomesh_coro_yield(void)
{
    struct picomesh_coro *self = *current_slot();
    if (!self) {
        ywarn("picomesh_coro_yield: called from main stack — no-op");
        return;
    }
    cothread_t back = self->resumer;
    *current_slot() = NULL;
    co_switch(back);
    /* When we come back, this coroutine is active again. */
    *current_slot() = self;
}

void picomesh_coro_resume(struct picomesh_coro *coro)
{
    if (!coro || coro->finished) return;
    /* Save and restore the caller's "current" around the switch so that
     * resume works when called from WITHIN another coroutine (nested
     * resume) — e.g. a cooperative lock handing off to the next waiter.
     * The resumed coro clears current_slot to NULL when it yields/
     * finishes; without restoring here, the nesting caller would keep
     * running with current_slot == NULL and its next loop_read/write
     * would latch the wrong (NULL) coro, losing the wakeup. When resume
     * is called from the loop's main stack `prev` is NULL, so existing
     * callers are unaffected. */
    struct picomesh_coro *prev = *current_slot();
    coro->resumer = co_active();
    *current_slot() = coro;
    co_switch(coro->thread);
    *current_slot() = prev;
}

void picomesh_coro_destroy(struct picomesh_coro *coro)
{
    if (!coro) return;
    if (!coro->finished) {
        ywarn("picomesh_coro_destroy: destroying unfinished coro id=%u", coro->id);
    }
    if (coro->thread) co_delete(coro->thread);
    free(coro->name);
    free(coro);
}

struct picomesh_coro *picomesh_coro_current(void)
{
    return *current_slot();
}

int picomesh_coro_is_finished(const struct picomesh_coro *coro)
{
    return coro ? coro->finished : 1;
}

unsigned int picomesh_coro_id(const struct picomesh_coro *coro)
{
    return coro ? coro->id : 0;
}

const char *picomesh_coro_name(const struct picomesh_coro *coro)
{
    return coro && coro->name ? coro->name : NULL;
}

void picomesh_coro_set_status(struct picomesh_coro *coro, int status)
{
    if (coro) coro->status = status;
}

int picomesh_coro_get_status(const struct picomesh_coro *coro)
{
    return coro ? coro->status : 0;
}
