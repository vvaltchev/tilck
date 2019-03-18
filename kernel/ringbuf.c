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
      .elems = 0,
      .max_elems = max_elems,
      .elem_size = elem_size,
      .buf = buf
   };
}

void ringbuf_destory(ringbuf *rb)
{
   bzero(rb, sizeof(ringbuf));
}

void ringbuf_reset(ringbuf *rb)
{
   rb->read_pos = rb->write_pos = rb->elems = 0;
}

bool ringbuf_write_elem(ringbuf *rb, void *elem_ptr)
{
   if (ringbuf_is_full(rb))
      return false;

   memcpy(rb->buf + rb->write_pos * rb->elem_size, elem_ptr, rb->elem_size);
   rb->write_pos = (rb->write_pos + 1) % rb->max_elems;
   rb->elems++;
   return true;
}

bool
ringbuf_write_elem_ex(ringbuf *rb, void *elem_ptr, bool *was_empty)
{
   if (ringbuf_is_full(rb))
      return false;

   *was_empty = ringbuf_is_empty(rb);

   memcpy(rb->buf + rb->write_pos * rb->elem_size, elem_ptr, rb->elem_size);
   rb->write_pos = (rb->write_pos + 1) % rb->max_elems;
   rb->elems++;
   return true;
}

bool ringbuf_read_elem(ringbuf *rb, void *elem_ptr /* out */)
{
   if (ringbuf_is_empty(rb))
      return false;

   memcpy(elem_ptr, rb->buf + rb->read_pos * rb->elem_size, rb->elem_size);
   rb->read_pos = (rb->read_pos + 1) % rb->max_elems;
   rb->elems--;
   return true;
}

bool ringbuf_write_elem1(ringbuf *rb, u8 val)
{
   if (ringbuf_is_full(rb))
      return false;

   rb->buf[rb->write_pos] = val;
   rb->write_pos = (rb->write_pos + 1) % rb->max_elems;
   rb->elems++;
   return true;
}

bool ringbuf_read_elem1(ringbuf *rb, u8 *elem_ptr)
{
   if (ringbuf_is_empty(rb))
      return false;

   *elem_ptr = rb->buf[rb->read_pos];
   rb->read_pos = (rb->read_pos + 1) % rb->max_elems;
   rb->elems--;
   return true;
}

bool ringbuf_unwrite_elem(ringbuf *rb, void *elem_ptr /* out */)
{
   if (ringbuf_is_empty(rb))
      return false;

   rb->write_pos = (rb->max_elems + rb->write_pos - 1) % rb->max_elems;
   rb->elems--;

   if (elem_ptr)
      memcpy(elem_ptr, rb->buf + rb->write_pos * rb->elem_size, rb->elem_size);

   return true;
}
