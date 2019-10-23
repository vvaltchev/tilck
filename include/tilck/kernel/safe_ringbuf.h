/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>

struct generic_safe_ringbuf_stat {

   union {

      struct {
         u32 read_pos : 15;
         u32 write_pos : 15;
         u32 full : 1;
         u32 avail_bit : 1;
      };

      ATOMIC(u32) raw;
   };

};

struct safe_ringbuf {

   u16 max_elems;
   u16 elem_size;
   struct generic_safe_ringbuf_stat s;
   u8 *buf;
};

static inline bool safe_ringbuf_is_empty(struct safe_ringbuf *rb)
{
   return rb->s.read_pos == rb->s.write_pos && !rb->s.full;
}

static inline bool safe_ringbuf_is_full(struct safe_ringbuf *rb)
{
   return rb->s.full;
}

void safe_ringbuf_init(struct safe_ringbuf *rb, u16 max_elems, u16 elem_size, void *b);
void safe_ringbuf_destory(struct safe_ringbuf *rb);
bool safe_ringbuf_write_elem(struct safe_ringbuf *rb, void *elem_ptr);
bool safe_ringbuf_write_elem_ex(struct safe_ringbuf *rb, void *elem, bool *was_empty);
bool safe_ringbuf_read_elem(struct safe_ringbuf *rb, void *elem_ptr /* out */);

bool safe_ringbuf_write_elem1(struct safe_ringbuf *rb, u8 val);
bool safe_ringbuf_read_elem1(struct safe_ringbuf *rb, u8 *elem_ptr);
