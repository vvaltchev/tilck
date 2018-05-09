
#pragma once
#include <common/basic_defs.h>

typedef struct {

   union {

      struct {
         u32 read_pos : 15;
         u32 write_pos : 15;
         u32 full : 1;
         u32 avail_bit : 1;
      };

      u32 raw;
   };

} generic_ringbuf_stat;

typedef struct {

   u16 max_elems;
   u16 elem_size;
   volatile generic_ringbuf_stat s;
   char *buf;

} ringbuf;

ALWAYS_INLINE bool ringbuf_is_empty(ringbuf *rb)
{
   return rb->s.read_pos == rb->s.write_pos && !rb->s.full;
}

ALWAYS_INLINE bool ringbuf_is_full(ringbuf *rb)
{
   return rb->s.full;
}

void ringbuf_init(ringbuf *rb, u16 max_elems, u16 elem_size, void *buf);
void ringbuf_destory(ringbuf *rb);
bool ringbuf_write_elem(ringbuf *rb, void *elem_ptr);
bool ringbuf_read_elem(ringbuf *rb, void *elem_ptr /* out */);

inline bool ringbuf_write_elem1(ringbuf *rb, u8 val)
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

inline bool ringbuf_read_elem1(ringbuf *rb, void *elem_ptr)
{
   generic_ringbuf_stat cs, ns;

   do {

      cs = rb->s;
      ns = rb->s;

      if (ringbuf_is_empty(rb))
         return false;

      *(u8 *)elem_ptr = rb->buf[cs.read_pos];

      ns.read_pos = (ns.read_pos + 1) % rb->max_elems;
      ns.full = false;

   } while (!BOOL_COMPARE_AND_SWAP(&rb->s.raw, cs.raw, ns.raw));

   return true;
}

inline bool ringbuf_unwrite_elem(ringbuf *rb)
{
   generic_ringbuf_stat cs, ns;

   do {

      cs = rb->s;
      ns = rb->s;

      if (ringbuf_is_empty(rb))
         return false;

      ns.write_pos = (ns.write_pos - 1) % rb->max_elems;
      ns.full = false;

   } while (!BOOL_COMPARE_AND_SWAP(&rb->s.raw, cs.raw, ns.raw));

   return true;
}
