/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/ringbuf.h>
#include <tilck/kernel/kmalloc.h>

void
ringbuf_init(ringbuf *rb, u32 max_elems, u32 elem_size, void *buf)
{
   *rb = (ringbuf) {
      .read_pos = 0,
      .write_pos = 0,
      .max_elems = max_elems,
      .elem_size = elem_size,
      .buf = buf,
      .full = false
   };
}

void ringbuf_destory(ringbuf *rb)
{
   bzero(rb, sizeof(ringbuf));
}

void ringbuf_reset(ringbuf *rb)
{
   rb->read_pos = rb->write_pos = 0;
   rb->full = false;
}

bool ringbuf_write_elem(ringbuf *rb, void *elem_ptr)
{
   if (rb->full)
      return false;

   rb->write_pos = (rb->write_pos + 1) % rb->max_elems;

   if (rb->write_pos == rb->read_pos)
      rb->full = true;

   memcpy(rb->buf + rb->write_pos * rb->elem_size, elem_ptr, rb->elem_size);
   return true;
}

bool
ringbuf_write_elem_ex(ringbuf *rb, void *elem_ptr, bool *was_empty)
{
   if (rb->full)
      return false;

   *was_empty = ringbuf_is_empty(rb);
   rb->write_pos = (rb->write_pos + 1) % rb->max_elems;

   if (rb->write_pos == rb->read_pos)
      rb->full = true;

   memcpy(rb->buf + rb->write_pos * rb->elem_size, elem_ptr, rb->elem_size);
   return true;
}

bool ringbuf_read_elem(ringbuf *rb, void *elem_ptr /* out */)
{
   if (ringbuf_is_empty(rb))
      return false;

   memcpy(elem_ptr, rb->buf + rb->read_pos * rb->elem_size, rb->elem_size);

   rb->read_pos = (rb->read_pos + 1) % rb->max_elems;
   rb->full = false;
   return true;
}

bool ringbuf_write_elem1(ringbuf *rb, u8 val)
{
   if (rb->full)
      return false;

   rb->write_pos = (rb->write_pos + 1) % rb->max_elems;

   if (rb->write_pos == rb->read_pos)
      rb->full = true;

   rb->buf[rb->write_pos] = val;
   return true;
}

bool ringbuf_read_elem1(ringbuf *rb, u8 *elem_ptr)
{
   if (ringbuf_is_empty(rb))
      return false;

   *elem_ptr = rb->buf[rb->read_pos];
   rb->read_pos = (rb->read_pos + 1) % rb->max_elems;
   rb->full = false;
   return true;
}

bool ringbuf_unwrite_elem(ringbuf *rb, void *elem_ptr /* out */)
{
   if (ringbuf_is_empty(rb))
      return false;

   memcpy(elem_ptr, rb->buf + rb->read_pos * rb->elem_size, rb->elem_size);
   rb->write_pos = (rb->write_pos - 1) % rb->max_elems;
   rb->full = false;
   return true;
}
