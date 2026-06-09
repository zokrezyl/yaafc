#ifndef PICOMESH_MSGPACK_MSGPACK_H
#define PICOMESH_MSGPACK_MSGPACK_H

/* Thin fixed-buffer adapter over the vendored cmp MessagePack codec.
 *
 * The msgpack frontend and the codegen-emitted minvoke / client glue all
 * encode into and decode out of a caller-provided buffer (a stack buffer on
 * the request path) — no heap, no streaming socket. This binds a cmp context
 * to such a buffer through a small cursor, so callers use the plain cmp_*
 * read/write API (cmp_read_integer, cmp_write_str, cmp_skip_object_no_limit,
 * …) directly. */

#include <cmp.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Cursor backing a cmp context. `offset` advances as bytes are consumed
 * (read) or produced (write); after writing, the encoded length is `offset`. */
struct picomesh_msgpack_buffer {
  uint8_t *data;
  size_t cap;
  size_t offset;
};

/* Bind `cmp` to read MessagePack from [data, data+len). The cmp_read_* calls
 * return false (and set cmp->error) past the end of the buffer. */
void picomesh_msgpack_reader_init(cmp_ctx_t *cmp,
                                  struct picomesh_msgpack_buffer *buf,
                                  const void *data, size_t len);

/* Bind `cmp` to write MessagePack into [data, data+cap). cmp_write_* return
 * false once the buffer is full; the produced length is `buf->offset`. */
void picomesh_msgpack_writer_init(cmp_ctx_t *cmp,
                                  struct picomesh_msgpack_buffer *buf,
                                  void *data, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* PICOMESH_MSGPACK_MSGPACK_H */
