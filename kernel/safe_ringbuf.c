/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/safe_ringbuf.h>
#include <tilck/kernel/kmalloc.h>

static ALWAYS_INLINE bool
rb_stat_is_empty(struct generic_safe_ringbuf_stat *s)
{
   return s->read_pos == s->write_pos && !s->full;
}

static ALWAYS_INLINE void
begin_debug_write_checks(struct safe_ringbuf *rb)
{
   DEBUG_ONLY(atomic_fetch_add_explicit(&rb->nested_writes, 1, mo_relaxed));
}

static ALWAYS_INLINE void
end_debug_write_checks(struct safe_ringbuf *rb)
{
   DEBUG_ONLY(atomic_fetch_sub_explicit(&rb->nested_writes, 1, mo_relaxed));
}

static ALWAYS_INLINE void
begin_debug_read_checks(struct safe_ringbuf *rb)
{
#ifdef DEBUG

   int nw = atomic_load_explicit(&rb->nested_writes, mo_relaxed);

   if (nw)
      panic("Read from safe_ringbuf interrupted on-going write. Not supported");

#endif
}

static ALWAYS_INLINE void
end_debug_read_checks(struct safe_ringbuf *rb)
{
   /* Do nothing, at the moment */
}


void
safe_ringbuf_init(struct safe_ringbuf *rb, u16 max_elems, u16 e_size, void *buf)
{
   ASSERT(max_elems <= 32768);

   rb->max_elems = max_elems;
   rb->elem_size = e_size;
   rb->buf = buf;
   rb->s.raw = 0;

#ifdef DEBUG
   rb->nested_writes = 0;
#endif
}

void safe_ringbuf_destory(struct safe_ringbuf *rb)
{
   bzero(rb, sizeof(struct safe_ringbuf));
}

bool
safe_ringbuf_write_elem(struct safe_ringbuf *rb,
                        void *elem_ptr,
                        bool *was_empty)
{
   struct generic_safe_ringbuf_stat cs, ns;
   bool ret = true;
   begin_debug_write_checks(rb);

   do {

      cs = rb->s;
      ns = rb->s;

      if (UNLIKELY(cs.full)) {
         *was_empty = false;
         ret = false;
         goto out;
      }

      ns.write_pos = (ns.write_pos + 1) % rb->max_elems;

      if (ns.write_pos == ns.read_pos)
         ns.full = true;

   } while (!atomic_cas_weak(&rb->s.raw,
                             (u32 *)&cs.raw,
                             ns.raw,
                             mo_relaxed,
                             mo_relaxed));

   memcpy(rb->buf + cs.write_pos * rb->elem_size, elem_ptr, rb->elem_size);
   *was_empty = rb_stat_is_empty(&cs);

out:
   end_debug_write_checks(rb);
   return ret;
}


bool safe_ringbuf_read_elem(struct safe_ringbuf *rb, void *elem_ptr /* out */)
{
   struct generic_safe_ringbuf_stat cs, ns;
   bool ret = true;
   begin_debug_read_checks(rb);

   do {

      cs = rb->s;
      ns = rb->s;

      if (rb_stat_is_empty(&cs)) {
         ret = false;
         goto out;
      }

      memcpy(elem_ptr, rb->buf + cs.read_pos * rb->elem_size, rb->elem_size);

      ns.read_pos = (ns.read_pos + 1) % rb->max_elems;
      ns.full = false;

   } while (!atomic_cas_weak(&rb->s.raw,
                             (u32 *)&cs.raw,
                             ns.raw,
                             mo_relaxed,
                             mo_relaxed));

out:
   end_debug_read_checks(rb);
   return ret;
}

bool safe_ringbuf_write_elem1(struct safe_ringbuf *rb, u8 val)
{
   struct generic_safe_ringbuf_stat cs, ns;
   ASSERT(rb->elem_size == 1);

   do {

      cs = rb->s;
      ns = rb->s;

      if (UNLIKELY(cs.full))
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

bool safe_ringbuf_read_elem1(struct safe_ringbuf *rb, u8 *elem_ptr)
{
   struct generic_safe_ringbuf_stat cs, ns;

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
