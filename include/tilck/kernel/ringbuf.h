/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

typedef struct {

   u32 read_pos;
   u32 write_pos;
   u32 elems;
   u32 max_elems;
   u32 elem_size;
   u8 *buf;

} ringbuf;

inline bool ringbuf_is_empty(ringbuf *rb)
{
   return rb->elems == 0;
}

inline bool ringbuf_is_full(ringbuf *rb)
{
   return rb->elems == rb->max_elems;
}

inline u32 ringbuf_get_elems(ringbuf *rb)
{
   return rb->elems;
}

inline void ringbuf_reset(ringbuf *rb)
{
   rb->read_pos = rb->write_pos = rb->elems = 0;
}

void ringbuf_init(ringbuf *rb, u32 max_elems, u32 elem_size, void *b);
void ringbuf_destory(ringbuf *rb);
bool ringbuf_write_elem(ringbuf *rb, void *elem_ptr);
bool ringbuf_read_elem(ringbuf *rb, void *elem_ptr /* out */);
bool ringbuf_unwrite_elem(ringbuf *rb, void *elem_ptr /* out */);

inline bool ringbuf_write_elem1(ringbuf *rb, u8 val)
{
   if (ringbuf_is_full(rb))
      return false;

   rb->buf[rb->write_pos] = val;
   rb->write_pos = (rb->write_pos + 1) % rb->max_elems;
   rb->elems++;
   return true;
}

inline bool ringbuf_read_elem1(ringbuf *rb, u8 *elem_ptr)
{
   if (ringbuf_is_empty(rb))
      return false;

   *elem_ptr = rb->buf[rb->read_pos];
   rb->read_pos = (rb->read_pos + 1) % rb->max_elems;
   rb->elems--;
   return true;
}
