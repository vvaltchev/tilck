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
      u32 __raw;
   };

};

struct safe_ringbuf {

   u16 max_elems;
   u16 elem_size;
   struct generic_safe_ringbuf_stat s;
   u8 *buf;

#ifdef DEBUG
   ATOMIC(int) nested_writes;
#endif
};

static inline bool safe_ringbuf_is_empty(struct safe_ringbuf *rb)
{
   return rb->s.read_pos == rb->s.write_pos && !rb->s.full;
}

static inline bool safe_ringbuf_is_full(struct safe_ringbuf *rb)
{
   return rb->s.full;
}

void
safe_ringbuf_init(struct safe_ringbuf *rb, u16 max_elems, u16 e_size, void *b);

void
safe_ringbuf_destory(struct safe_ringbuf *rb);

/* Generic read/write funcs */

bool
safe_ringbuf_write_elem(struct safe_ringbuf *rb, void *e, bool *was_empty);

bool
safe_ringbuf_read_elem(struct safe_ringbuf *rb, void *elem_ptr /* out */);


/* Pointer-size read/write funcs */

bool
safe_ringbuf_write_ulong(struct safe_ringbuf *rb, void *e, bool *was_empty);

bool
safe_ringbuf_read_ulong(struct safe_ringbuf *rb, void *elem_ptr /* out */);


/* Size-specific read/write funcs */

bool safe_ringbuf_write_1(struct safe_ringbuf *rb, void *e, bool *was_empty);
bool safe_ringbuf_write_2(struct safe_ringbuf *rb, void *e, bool *was_empty);
bool safe_ringbuf_write_4(struct safe_ringbuf *rb, void *e, bool *was_empty);
bool safe_ringbuf_write_8(struct safe_ringbuf *rb, void *e, bool *was_empty);

bool safe_ringbuf_read_1(struct safe_ringbuf *rb, void *elem_ptr /* out */);
bool safe_ringbuf_read_2(struct safe_ringbuf *rb, void *elem_ptr /* out */);
bool safe_ringbuf_read_4(struct safe_ringbuf *rb, void *elem_ptr /* out */);
bool safe_ringbuf_read_8(struct safe_ringbuf *rb, void *elem_ptr /* out */);
