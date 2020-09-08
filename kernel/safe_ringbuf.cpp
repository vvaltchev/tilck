/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * WARNING: despite the name, safe_ringbuf is not SAFE in every use pattern.
 * Read at the documentation in the header file: safe_ringbuf.h.
 */

extern "C" {
#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/atomics.h>
#include <tilck/kernel/safe_ringbuf.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/hal.h>
}

#include <tilck/common/cpputils.h>

static ALWAYS_INLINE bool
rb_stat_is_empty(struct generic_safe_ringbuf_stat *s)
{
   return s->read_pos == s->write_pos && !s->full;
}

bool safe_ringbuf_is_empty(struct safe_ringbuf *rb)
{
   bool res;
   ulong var;

   disable_interrupts(&var);
   {
      res = rb_stat_is_empty(&rb->s);
   }
   enable_interrupts(&var);
   return res;
}

bool safe_ringbuf_is_full(struct safe_ringbuf *rb)
{
   struct generic_safe_ringbuf_stat cs;
   cs.__raw = atomic_load_explicit(&rb->s.raw, mo_relaxed);
   return cs.full;
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
#if DEBUG_CHECKS

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

#if DEBUG_CHECKS
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
   typedef typename unsigned_by_size<static_elem_size>::type T;
   struct generic_safe_ringbuf_stat cs, ns;
   const u16 e_size = rb->elem_size;
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

   if (static_elem_size)
      ((T *)rb->buf)[cs.write_pos] = *(T *)elem_ptr;
   else
      memcpy(rb->buf + cs.write_pos * e_size, elem_ptr, e_size);

   *was_empty = rb_stat_is_empty(&cs);

out:
   end_debug_write_checks(rb);
   return ret;
}

template <int static_elem_size = 0>
static ALWAYS_INLINE bool
__safe_ringbuf_read(struct safe_ringbuf *rb, void *elem_ptr /* out */)
{
   typedef typename unsigned_by_size<static_elem_size>::type T;
   struct generic_safe_ringbuf_stat cs, ns;
   const u16 e_size = rb->elem_size;
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

      if (static_elem_size)
         *(T *)elem_ptr = ((T *)rb->buf)[cs.read_pos];
      else
         memcpy(elem_ptr, rb->buf + cs.read_pos * e_size, e_size);

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

#define INST_WRITE_FUNC(s, n)                                                  \
   bool safe_ringbuf_write_##s(struct safe_ringbuf *rb, void *e, bool *empty) {\
      return __safe_ringbuf_write<n>(rb, e, empty);                            \
   }

#define INST_READ_FUNC(s, n)                                                   \
   bool safe_ringbuf_read_##s(struct safe_ringbuf *rb, void *elem_ptr) {       \
      return __safe_ringbuf_read<n>(rb, elem_ptr);                             \
   }


// For the moment, the following instantiations are NOT needed in Tilck.
// No point in adding code-bloat to the kernel. As a use-cases come out,
// un-comment the individual functions.

// INST_WRITE_FUNC(ulong, sizeof(void *))
INST_WRITE_FUNC(1, 1)
// INST_WRITE_FUNC(2, 2)
// INST_WRITE_FUNC(4, 4)
// INST_WRITE_FUNC(8, 8)

// INST_READ_FUNC(ulong, sizeof(void *))
INST_READ_FUNC(1, 1)
// INST_READ_FUNC(2, 2)
// INST_READ_FUNC(4, 4)
// INST_READ_FUNC(8, 8)

} // extern "C"
