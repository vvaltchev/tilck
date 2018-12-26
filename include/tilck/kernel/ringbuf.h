/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>

typedef struct {

   union {

      struct {
         u32 read_pos : 15;
         u32 write_pos : 15;
         u32 full : 1;
         u32 avail_bit : 1;
      };

      ATOMIC(u32) raw;
   };

} generic_ringbuf_stat;

typedef struct {

   u16 max_elems;
   u16 elem_size;
   volatile generic_ringbuf_stat s;
   char *buf;

} ringbuf;

static inline bool ringbuf_is_empty(ringbuf *rb)
{
   return rb->s.read_pos == rb->s.write_pos && !rb->s.full;
}

static inline bool ringbuf_is_full(ringbuf *rb)
{
   return rb->s.full;
}

void ringbuf_init(ringbuf *rb, u16 max_elems, u16 elem_size, void *buf);
void ringbuf_destory(ringbuf *rb);
bool ringbuf_write_elem(ringbuf *rb, void *elem_ptr);
bool ringbuf_read_elem(ringbuf *rb, void *elem_ptr /* out */);
bool ringbuf_unwrite_elem(ringbuf *rb, void *elem_ptr /* out */);

bool ringbuf_write_elem1(ringbuf *rb, u8 val);
bool ringbuf_read_elem1(ringbuf *rb, u8 *elem_ptr);
bool ringbuf_write_elem_ex(ringbuf *rb, void *elem_ptr, bool *was_empty);
