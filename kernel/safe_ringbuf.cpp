/* SPDX-License-Identifier: BSD-2-Clause */

extern "C" {
#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/atomics.h>
#include <tilck/kernel/safe_ringbuf.h>
#include <tilck/kernel/kmalloc.h>
}

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

extern "C" {

void
safe_ringbuf_init(struct safe_ringbuf *rb, u16 max_elems, u16 e_size, void *buf)
{
   ASSERT(max_elems <= 32768);

   rb->max_elems = max_elems;
   rb->elem_size = e_size;
   rb->buf = (u8 *)buf;
   rb->s.raw = 0;

#ifdef DEBUG
   rb->nested_writes = 0;
#endif
}

void safe_ringbuf_destory(struct safe_ringbuf *rb)
{
   bzero(rb, sizeof(struct safe_ringbuf));
}

} // extern "C"

template <int static_elem_size = 0>
static ALWAYS_INLINE bool
__safe_ringbuf_write(struct safe_ringbuf *rb,
                     void *elem_ptr,
                     bool *was_empty)
{
   struct generic_safe_ringbuf_stat cs, ns;
   bool ret = true;
   begin_debug_write_checks(rb);

   do {

      cs.__raw = rb->s.__raw;
      ns.__raw = rb->s.__raw;

      if (UNLIKELY(cs.full)) {
         *was_empty = false;
         ret = false;
         goto out;
      }

      ns.write_pos = (ns.write_pos + 1) % rb->max_elems;

      if (ns.write_pos == ns.read_pos)
         ns.full = true;

   } while (!atomic_cas_weak(&rb->s.raw,
                             &cs.__raw,
                             ns.__raw,
                             mo_relaxed,
                             mo_relaxed));

   if (static_elem_size == 1)
      ((u8 *)rb->buf)[cs.write_pos]  = *(u8 *)elem_ptr;
   else if (static_elem_size == 2)
      ((u16 *)rb->buf)[cs.write_pos] = *(u16 *)elem_ptr;
   else if (static_elem_size == 4)
      ((u32 *)rb->buf)[cs.write_pos] = *(u32 *)elem_ptr;
   else if (static_elem_size == 8)
      ((u64 *)rb->buf)[cs.write_pos] = *(u64 *)elem_ptr;
   else
      memcpy(rb->buf + cs.write_pos * rb->elem_size, elem_ptr, rb->elem_size);

   *was_empty = rb_stat_is_empty(&cs);

out:
   end_debug_write_checks(rb);
   return ret;
}

template <int static_elem_size = 0>
static ALWAYS_INLINE bool
__safe_ringbuf_read(struct safe_ringbuf *rb, void *elem_ptr /* out */)
{
   struct generic_safe_ringbuf_stat cs, ns;
   bool ret = true;

   if (static_elem_size)
      ASSERT(rb->elem_size == static_elem_size);

   begin_debug_read_checks(rb);

   do {

      cs.__raw = rb->s.__raw;
      ns.__raw = rb->s.__raw;

      if (rb_stat_is_empty(&cs)) {
         ret = false;
         goto out;
      }

      if (static_elem_size == 1)
         *(u8 *)elem_ptr =  (( u8 *)rb->buf)[cs.read_pos];
      else if (static_elem_size == 2)
         *(u16 *)elem_ptr = ((u16 *)rb->buf)[cs.read_pos];
      else if (static_elem_size == 4)
         *(u32 *)elem_ptr = ((u32 *)rb->buf)[cs.read_pos];
      else if (static_elem_size == 8)
         *(u64 *)elem_ptr = ((u64 *)rb->buf)[cs.read_pos];
      else
         memcpy(elem_ptr, rb->buf + cs.read_pos * rb->elem_size, rb->elem_size);

      ns.read_pos = (ns.read_pos + 1) % rb->max_elems;
      ns.full = false;

   } while (!atomic_cas_weak(&rb->s.raw,
                             &cs.__raw,
                             ns.__raw,
                             mo_relaxed,
                             mo_relaxed));

out:
   end_debug_read_checks(rb);
   return ret;
}

extern "C" {

bool
safe_ringbuf_write_elem(struct safe_ringbuf *rb, void *e, bool *was_empty)
{
   return __safe_ringbuf_write<>(rb, e, was_empty);
}


bool
safe_ringbuf_read_elem(struct safe_ringbuf *rb, void *elem_ptr /* out */)
{
   return __safe_ringbuf_read<>(rb, elem_ptr);
}

#if 0

/*
 * For the moment, the following instantiations are NOT needed in Tilck.
 * No point in adding code-bloat to the kernel. As a use-case comes out,
 * move the individual functions outside of this #if 0 block.
 */

bool
safe_ringbuf_write_ulong(struct safe_ringbuf *rb, void *e, bool *was_empty)
{
   return __safe_ringbuf_write<sizeof(void *)>(rb, e, was_empty);
}

bool
safe_ringbuf_read_ulong(struct safe_ringbuf *rb, void *elem_ptr /* out */)
{
   return __safe_ringbuf_read<sizeof(void *)>(rb, elem_ptr);
}

bool
safe_ringbuf_write1(struct safe_ringbuf *rb, void *e, bool *was_empty)
{
   return __safe_ringbuf_write<1>(rb, e, was_empty);
}

bool
safe_ringbuf_write2(struct safe_ringbuf *rb, void *e, bool *was_empty)
{
   return __safe_ringbuf_write<2>(rb, e, was_empty);
}

bool
safe_ringbuf_write4(struct safe_ringbuf *rb, void *e, bool *was_empty)
{
   return __safe_ringbuf_write<4>(rb, e, was_empty);
}

bool
safe_ringbuf_write8(struct safe_ringbuf *rb, void *e, bool *was_empty)
{
   return __safe_ringbuf_write<8>(rb, e, was_empty);
}

bool
safe_ringbuf_read1(struct safe_ringbuf *rb, void *elem_ptr /* out */)
{
   return __safe_ringbuf_read<1>(rb, elem_ptr);
}

bool
safe_ringbuf_read2(struct safe_ringbuf *rb, void *elem_ptr /* out */)
{
   return __safe_ringbuf_read<2>(rb, elem_ptr);
}

bool
safe_ringbuf_read4(struct safe_ringbuf *rb, void *elem_ptr /* out */)
{
   return __safe_ringbuf_read<4>(rb, elem_ptr);
}

bool
safe_ringbuf_read8(struct safe_ringbuf *rb, void *elem_ptr /* out */)
{
   return __safe_ringbuf_read<8>(rb, elem_ptr);
}

#endif // #if 0

} // extern "C"
