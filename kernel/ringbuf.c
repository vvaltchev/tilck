/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/kernel/ringbuf.h>
#include <tilck/kernel/kmalloc.h>

static inline bool is_empty(generic_ringbuf_stat *s)
{
   return s->read_pos == s->write_pos && !s->full;
}

void ringbuf_init(ringbuf *rb, u16 max_elems, u16 elem_size, void *buf)
{
   ASSERT(max_elems <= 32768);

   rb->max_elems = max_elems;
   rb->elem_size = elem_size;
   rb->buf = buf;
   rb->s.raw = 0;
}

void ringbuf_destory(ringbuf *rb)
{
   bzero(rb, sizeof(ringbuf));
}

bool ringbuf_write_elem(ringbuf *rb, void *elem_ptr)
{
   generic_ringbuf_stat cs, ns;

   do {

      cs = rb->s;
      ns = rb->s;

      if (cs.full)
         return false;

      ns.write_pos = (ns.write_pos + 1) % rb->max_elems;

      if (ns.write_pos == ns.read_pos)
         ns.full = true;

   } while (!BOOL_COMPARE_AND_SWAP(&rb->s.raw, cs.raw, ns.raw));

   memcpy(rb->buf + cs.write_pos * rb->elem_size, elem_ptr, rb->elem_size);
   return true;
}


bool ringbuf_write_elem_ex(ringbuf *rb, void *elem_ptr, bool *was_empty)
{
   generic_ringbuf_stat cs, ns;

   do {

      cs = rb->s;
      ns = rb->s;

      if (cs.full)
         return false;

      *was_empty = is_empty(&cs);
      ns.write_pos = (ns.write_pos + 1) % rb->max_elems;

      if (ns.write_pos == ns.read_pos)
         ns.full = true;

   } while (!BOOL_COMPARE_AND_SWAP(&rb->s.raw, cs.raw, ns.raw));

   memcpy(rb->buf + cs.write_pos * rb->elem_size, elem_ptr, rb->elem_size);
   return true;
}


bool ringbuf_read_elem(ringbuf *rb, void *elem_ptr /* out */)
{
   generic_ringbuf_stat cs, ns;

   do {

      cs = rb->s;
      ns = rb->s;

      if (is_empty(&cs))
         return false;

      memcpy(elem_ptr, rb->buf + cs.read_pos * rb->elem_size, rb->elem_size);

      ns.read_pos = (ns.read_pos + 1) % rb->max_elems;
      ns.full = false;

   } while (!BOOL_COMPARE_AND_SWAP(&rb->s.raw, cs.raw, ns.raw));

   return true;
}

bool ringbuf_write_elem1(ringbuf *rb, u8 val)
{
   generic_ringbuf_stat cs, ns;
   ASSERT(rb->elem_size == 1);

   do {

      cs = rb->s;
      ns = rb->s;

      if (cs.full)
         return false;

      ns.write_pos = (ns.write_pos + 1) % rb->max_elems;

      if (ns.write_pos == ns.read_pos)
         ns.full = true;

   } while (!BOOL_COMPARE_AND_SWAP(&rb->s.raw, cs.raw, ns.raw));

   rb->buf[cs.write_pos] = val;
   return true;
}

bool ringbuf_read_elem1(ringbuf *rb, u8 *elem_ptr)
{
   generic_ringbuf_stat cs, ns;

   do {

      cs = rb->s;
      ns = rb->s;

      if (is_empty(&cs))
         return false;

      *elem_ptr = rb->buf[cs.read_pos];

      ns.read_pos = (ns.read_pos + 1) % rb->max_elems;
      ns.full = false;

   } while (!BOOL_COMPARE_AND_SWAP(&rb->s.raw, cs.raw, ns.raw));

   return true;
}

bool ringbuf_unwrite_elem(ringbuf *rb, void *elem_ptr /* out */)
{
   generic_ringbuf_stat cs, ns;

   do {

      cs = rb->s;
      ns = rb->s;

      if (is_empty(&cs))
         return false;

      memcpy(elem_ptr, rb->buf + cs.read_pos * rb->elem_size, rb->elem_size);
      ns.write_pos = (ns.write_pos - 1) % rb->max_elems;
      ns.full = false;

   } while (!BOOL_COMPARE_AND_SWAP(&rb->s.raw, cs.raw, ns.raw));

   return true;
}
