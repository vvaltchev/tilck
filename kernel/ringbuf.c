/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/ringbuf.h>
#include <tilck/kernel/kmalloc.h>

static inline bool rb_stat_is_empty(generic_safe_ringbuf_stat *s)
{
   return s->read_pos == s->write_pos && !s->full;
}

void
safe_ringbuf_init(safe_ringbuf *rb, u16 max_elems, u16 elem_size, void *buf)
{
   ASSERT(max_elems <= 32768);

   rb->max_elems = max_elems;
   rb->elem_size = elem_size;
   rb->buf = buf;
   rb->s.raw = 0;
}

void safe_ringbuf_destory(safe_ringbuf *rb)
{
   bzero(rb, sizeof(safe_ringbuf));
}

void safe_ringbuf_reset(safe_ringbuf *rb)
{
   atomic_store_explicit(&rb->s.raw, 0, mo_relaxed);
}

bool safe_ringbuf_write_elem(safe_ringbuf *rb, void *elem_ptr)
{
   generic_safe_ringbuf_stat cs, ns;

   do {

      cs = rb->s;
      ns = rb->s;

      if (cs.full)
         return false;

      ns.write_pos = (ns.write_pos + 1) % rb->max_elems;

      if (ns.write_pos == ns.read_pos)
         ns.full = true;

   } while (!atomic_cas_weak(&rb->s.raw,
                             (u32 *)&cs.raw,
                             ns.raw,
                             mo_relaxed,
                             mo_relaxed));

   memcpy(rb->buf + cs.write_pos * rb->elem_size, elem_ptr, rb->elem_size);
   return true;
}


bool
safe_ringbuf_write_elem_ex(safe_ringbuf *rb, void *elem_ptr, bool *was_empty)
{
   generic_safe_ringbuf_stat cs, ns;

   do {

      cs = rb->s;
      ns = rb->s;

      if (cs.full)
         return false;

      *was_empty = rb_stat_is_empty(&cs);
      ns.write_pos = (ns.write_pos + 1) % rb->max_elems;

      if (ns.write_pos == ns.read_pos)
         ns.full = true;

   } while (!atomic_cas_weak(&rb->s.raw,
                             (u32 *)&cs.raw,
                             ns.raw,
                             mo_relaxed,
                             mo_relaxed));

   memcpy(rb->buf + cs.write_pos * rb->elem_size, elem_ptr, rb->elem_size);
   return true;
}


bool safe_ringbuf_read_elem(safe_ringbuf *rb, void *elem_ptr /* out */)
{
   generic_safe_ringbuf_stat cs, ns;

   do {

      cs = rb->s;
      ns = rb->s;

      if (rb_stat_is_empty(&cs))
         return false;

      memcpy(elem_ptr, rb->buf + cs.read_pos * rb->elem_size, rb->elem_size);

      ns.read_pos = (ns.read_pos + 1) % rb->max_elems;
      ns.full = false;

   } while (!atomic_cas_weak(&rb->s.raw,
                             (u32 *)&cs.raw,
                             ns.raw,
                             mo_relaxed,
                             mo_relaxed));

   return true;
}

bool safe_ringbuf_write_elem1(safe_ringbuf *rb, u8 val)
{
   generic_safe_ringbuf_stat cs, ns;
   ASSERT(rb->elem_size == 1);

   do {

      cs = rb->s;
      ns = rb->s;

      if (cs.full)
         return false;

      ns.write_pos = (ns.write_pos + 1) % rb->max_elems;

      if (ns.write_pos == ns.read_pos)
         ns.full = true;

   } while (!atomic_cas_weak(&rb->s.raw,
                             (u32 *)&cs.raw,
                             ns.raw,
                             mo_relaxed,
                             mo_relaxed));

   rb->buf[cs.write_pos] = val;
   return true;
}

bool safe_ringbuf_read_elem1(safe_ringbuf *rb, u8 *elem_ptr)
{
   generic_safe_ringbuf_stat cs, ns;

   do {

      cs = rb->s;
      ns = rb->s;

      if (rb_stat_is_empty(&cs))
         return false;

      *elem_ptr = rb->buf[cs.read_pos];

      ns.read_pos = (ns.read_pos + 1) % rb->max_elems;
      ns.full = false;

   } while (!atomic_cas_weak(&rb->s.raw,
                             (u32 *)&cs.raw,
                             ns.raw,
                             mo_relaxed,
                             mo_relaxed));

   return true;
}

bool safe_ringbuf_unwrite_elem(safe_ringbuf *rb, void *elem_ptr /* out */)
{
   generic_safe_ringbuf_stat cs, ns;

   do {

      cs = rb->s;
      ns = rb->s;

      if (rb_stat_is_empty(&cs))
         return false;

      memcpy(elem_ptr, rb->buf + cs.read_pos * rb->elem_size, rb->elem_size);
      ns.write_pos = (ns.write_pos - 1) % rb->max_elems;
      ns.full = false;

   } while (!atomic_cas_weak(&rb->s.raw,
                             (u32 *)&cs.raw,
                             ns.raw,
                             mo_relaxed,
                             mo_relaxed));

   return true;
}
