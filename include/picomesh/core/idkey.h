/* idkey — the generic shard-key hash shared by relational-storage consumers.
 *
 * Sharding correctness depends on every consumer computing the SAME shard key
 * from the SAME bytes (see docs/sharded-relational-storage.md, "Key contract").
 * This header provides ONLY that primitive — `picomesh_fnv1a32` over a byte
 * string — used by the lookup-sharded plugins to route a session id / refresh
 * token / username to its shard.
 *
 * It deliberately does NOT define application identity (e.g. "uid from
 * username"): how a uid is derived or assigned is the accounts plugin's domain,
 * not a globally-shared primitive and never the transport frontend's concern.
 */

#ifndef PICOMESH_CORE_IDKEY_H
#define PICOMESH_CORE_IDKEY_H

#include <stdint.h>

/* 32-bit FNV-1a over the NUL-terminated bytes of `s` (NULL → offset basis). */
static inline uint32_t picomesh_fnv1a32(const char *s) {
  uint32_t hash = 2166136261u;
  if (s)
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
      hash ^= (uint32_t)*p;
      hash *= 16777619u;
    }
  return hash;
}

#endif /* PICOMESH_CORE_IDKEY_H */
