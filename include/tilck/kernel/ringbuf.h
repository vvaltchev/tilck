/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

typedef struct {

   u32 read_pos;
   u32 write_pos;
   u32 max_elems;
   u32 elem_size;
   u8 *buf;
   bool full : 1;

} ringbuf;

static inline bool ringbuf_is_empty(ringbuf *rb)
{
   return rb->read_pos == rb->write_pos && !rb->full;
}

static inline bool ringbuf_is_full(ringbuf *rb)
{
   return rb->full;
}

void ringbuf_init(ringbuf *rb, u32 max_elems, u32 elem_size, void *b);
void ringbuf_destory(ringbuf *rb);
void ringbuf_reset(ringbuf *rb);
bool ringbuf_write_elem(ringbuf *rb, void *elem_ptr);
bool ringbuf_read_elem(ringbuf *rb, void *elem_ptr /* out */);
bool ringbuf_unwrite_elem(ringbuf *rb, void *elem_ptr /* out */);

bool ringbuf_write_elem1(ringbuf *rb, u8 val);
bool ringbuf_read_elem1(ringbuf *rb, u8 *elem_ptr);
bool ringbuf_write_elem_ex(ringbuf *rb, void *elem, bool *was_empty);
