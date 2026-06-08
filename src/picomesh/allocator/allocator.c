/* picomesh_allocator — per-thread, size-classed pool allocator.
 * See include/picomesh/allocator/allocator.h for the model and threading rules. */

#define _POSIX_C_SOURCE 200809L

#include <picomesh/allocator/allocator.h>
#include <picomesh/ycore/ytrace.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* One chunk pulled from the process allocator, carved into blocks. Chunks are
 * never returned individually — they live until the allocator is destroyed. */
#define PICOMESH_ALLOC_CHUNK_BYTES (1u << 20) /* 1 MiB per chunk */

#define BLOCK_MAGIC 0x504d4253u /* "PMBS" */
#define KLASS_DIRECT 0u         /* sentinel: served by a direct malloc */

/* Header in front of every block. The payload (what the caller sees) starts
 * right after it. Kept at 16 bytes so the payload stays 16-byte aligned for any
 * C type. When a block is free its payload holds a free_node (see below), so no
 * extra per-block bookkeeping is needed. */
struct block_header {
    uint32_t klass; /* size class 1..MAX, or KLASS_DIRECT for a malloc passthrough */
    uint32_t magic;
    struct picomesh_allocator *owner;
};

/* Free-list link, overlaid on a freed block's payload. */
struct free_node {
    struct free_node *next;
};

struct chunk {
    struct chunk *next;
    /* chunk bytes follow this header */
};

struct picomesh_allocator {
    struct free_node *freelist[PICOMESH_ALLOC_MAX_CLASS + 1]; /* indexed by klass */
    char *bump;     /* next carve position in the current chunk */
    char *bump_end; /* end of the current chunk */
    struct chunk *chunks;
};

#define HEADER_BYTES ((size_t)sizeof(struct block_header))

struct picomesh_allocator *picomesh_allocator_create(void)
{
    return calloc(1, sizeof(struct picomesh_allocator));
}

void picomesh_allocator_destroy(struct picomesh_allocator *pool)
{
    if (!pool) return;
    struct chunk *chunk = pool->chunks;
    while (chunk) {
        struct chunk *next = chunk->next;
        free(chunk);
        chunk = next;
    }
    free(pool);
}

struct picomesh_allocator *picomesh_allocator_thread(void)
{
    /* Thread-local: each loop/worker thread carves from its own pool with no
     * lock. Created on first use, lives for the thread's lifetime (the loop and
     * worker threads are process-lifetime, so the storage is reclaimed at
     * exit). Mirrors the thread-local pattern in yco/coro.c. */
    static _Thread_local struct picomesh_allocator *self = NULL;
    if (!self) self = picomesh_allocator_create();
    return self;
}

/* Pull a fresh chunk (at least `need` usable bytes) from the process allocator
 * and make it the current carve target. Returns 0 on OOM. */
static int grow(struct picomesh_allocator *pool, size_t need)
{
    size_t body = PICOMESH_ALLOC_CHUNK_BYTES;
    if (need > body) body = need;
    struct chunk *chunk = malloc(sizeof(struct chunk) + body);
    if (!chunk) return 0;
    chunk->next = pool->chunks;
    pool->chunks = chunk;
    pool->bump = (char *)chunk + sizeof(struct chunk);
    pool->bump_end = pool->bump + body;
    return 1;
}

void *picomesh_allocator_alloc(struct picomesh_allocator *pool, size_t size)
{
    if (!pool) return NULL;
    size_t total = HEADER_BYTES + size;
    size_t klass = (total + PICOMESH_ALLOC_BASE - 1) / PICOMESH_ALLOC_BASE;

    /* Too large for any pooled class: hand off to the process allocator. The
     * KLASS_DIRECT header lets free() route it straight back to free(). */
    if (klass > PICOMESH_ALLOC_MAX_CLASS) {
        struct block_header *header = malloc(total);
        if (!header) return NULL;
        header->klass = KLASS_DIRECT;
        header->magic = BLOCK_MAGIC;
        header->owner = pool;
        return (char *)header + HEADER_BYTES;
    }

    /* Reuse a recycled block of this class if one is on the free-list. The
     * header is still intact from when the block was first carved. */
    if (pool->freelist[klass]) {
        struct free_node *node = pool->freelist[klass];
        pool->freelist[klass] = node->next;
        return node; /* payload address == free_node address */
    }

    /* Carve a fresh block from the current chunk. */
    size_t block_bytes = klass * PICOMESH_ALLOC_BASE;
    if ((size_t)(pool->bump_end - pool->bump) < block_bytes) {
        if (!grow(pool, block_bytes)) return NULL;
    }
    struct block_header *header = (struct block_header *)pool->bump;
    pool->bump += block_bytes;
    header->klass = (uint32_t)klass;
    header->magic = BLOCK_MAGIC;
    header->owner = pool;
    return (char *)header + HEADER_BYTES;
}

void picomesh_allocator_free(struct picomesh_allocator *pool, void *ptr)
{
    if (!ptr) return;
    struct block_header *header = (struct block_header *)((char *)ptr - HEADER_BYTES);
    if (header->magic != BLOCK_MAGIC) {
        ywarn("picomesh_allocator_free: bad/corrupt block header (double free or foreign ptr)");
        return;
    }
    if (header->owner != pool) {
        /* Cross-thread free would corrupt another thread's free-list. Route it
         * to the owning pool instead and warn — this is a caller bug. */
        ywarn("picomesh_allocator_free: block freed on a different pool than it was allocated on");
        pool = header->owner;
    }
    if (header->klass == KLASS_DIRECT) {
        header->magic = 0;
        free(header);
        return;
    }
    struct free_node *node = (struct free_node *)ptr;
    node->next = pool->freelist[header->klass];
    pool->freelist[header->klass] = node;
}
