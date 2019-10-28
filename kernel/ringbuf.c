/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/ringbuf.h>
#include <tilck/kernel/kmalloc.h>

extern inline void ringbuf_reset(struct ringbuf *rb);
extern inline bool ringbuf_write_elem1(struct ringbuf *rb, u8 val);
extern inline bool ringbuf_read_elem1(struct ringbuf *rb, u8 *elem_ptr);
extern inline bool ringbuf_is_empty(struct ringbuf *rb);
extern inline bool ringbuf_is_full(struct ringbuf *rb);
extern inline u32 ringbuf_get_elems(struct ringbuf *rb);

void
ringbuf_init(struct ringbuf *rb, u32 max_elems, u32 elem_size, void *buf)
{
   *rb = (struct ringbuf) {
      .read_pos = 0,
      .write_pos = 0,
      .elems = 0,
      .max_elems = max_elems,
      .elem_size = elem_size,
      .buf = buf,
   };
}

void ringbuf_destory(struct ringbuf *rb)
{
   bzero(rb, sizeof(struct ringbuf));
}

bool ringbuf_write_elem(struct ringbuf *rb, void *elem_ptr)
{
   if (ringbuf_is_full(rb))
      return false;

   memcpy(rb->buf + rb->write_pos * rb->elem_size, elem_ptr, rb->elem_size);
   rb->write_pos = (rb->write_pos + 1) % rb->max_elems;
   rb->elems++;
   return true;
}

u32 ringbuf_write_bytes(struct ringbuf *rb, u8 *buf, u32 len)
{
   u32 actual_len;
   u32 actual_len2;
   ASSERT(rb->elem_size == 1);

   if (ringbuf_is_full(rb))
      return 0;

   /*
    * Three cases:
    *
    * 1) +-----------------------------------------------------------+
    *              ^                       ^
    *          write_pos                read_pos
    *
    * 2) +-----------------------------------------------------------+
    *              ^                       ^
    *          read_pos                write_pos
    *
    * 3) +-----------------------------------------------------------+
    *                          ^
    *                read_pos == write_pos
    *
    * Case 1:
    *    We can write at most (read_pos - write_pos) bytes and set at the end
    *    write_pos == read_pos.
    *
    * Case 2:
    *    We can write the data in two chunks.
    *    First, write (max_elems - write_pos) bytes, moving write_pos to the
    *    end of the buffer. Second, we can start from the beginning of the buf
    *    and write `read_pos` bytes, until write_pos == read_pos.
    *
    * Case 3:
    *    Because we checked that the ringbuffer is *not* full, it is just a
    *    special variant of case 2.
    *
    */

   if (rb->write_pos < rb->read_pos) {

      actual_len = MIN(len, rb->read_pos - rb->write_pos);
      memcpy(rb->buf + rb->write_pos, buf, actual_len);
      rb->write_pos += actual_len;
      rb->elems += actual_len;
      return actual_len;
   }

   /* Part one */
   actual_len = MIN(len, rb->max_elems - rb->write_pos);
   memcpy(rb->buf + rb->write_pos, buf, actual_len);
   rb->write_pos = (rb->write_pos + actual_len) % rb->max_elems;
   rb->elems += actual_len;

   /* `actual_len` can be either less than or equal to `len` */
   if (actual_len == len)
      return actual_len; /* it's equal to, we're done */

   /* `actual_len` was less than `len`, we have to continue */

   /* Part two */
   ASSERT(rb->write_pos == 0);
   actual_len2 = MIN(len - actual_len, rb->read_pos);
   memcpy(rb->buf, buf + actual_len, actual_len2);
   rb->write_pos += actual_len2;
   rb->elems += actual_len2;

   return actual_len + actual_len2;
}

u32 ringbuf_read_bytes(struct ringbuf *rb, u8 *buf, u32 len)
{
   u32 actual_len;
   u32 actual_len2;
   ASSERT(rb->elem_size == 1);

   if (ringbuf_is_empty(rb))
      return 0;

   /*
    * Three cases:
    *
    * 1) +-----------------------------------------------------------+
    *              ^                       ^
    *          read_pos                write_pos
    *
    * 2) +-----------------------------------------------------------+
    *              ^                       ^
    *          write_pos                read_pos
    *
    * 3) +-----------------------------------------------------------+
    *                          ^
    *                read_pos == write_pos
    *
    * Case 1:
    *    We can read at most (write_pos - read_pos) bytes.
    *
    * Case 2:
    *    We can read at first (max_elems - read_pos) and then `write_pos`
    *    bytes.
    *
    * Case 3:
    *    Because we checked that the ringbug is *not* empty, it is just a
    *    special variant of case 2.
    */

   if (rb->read_pos < rb->write_pos) {

      actual_len = MIN(len, rb->write_pos - rb->read_pos);
      memcpy(buf, rb->buf + rb->read_pos, actual_len);
      rb->read_pos += actual_len;
      rb->elems -= actual_len;
      return actual_len;
   }

   /* Part one */
   actual_len = MIN(len, rb->max_elems - rb->read_pos);
   memcpy(buf, rb->buf + rb->read_pos, actual_len);
   rb->read_pos = (rb->read_pos + actual_len) % rb->max_elems;
   rb->elems -= actual_len;

   /* `actual_len` can be either less than or equal to `len` */
   if (actual_len == len)
      return actual_len; /* it's equal to, we're done */

   /* `actual_len` was less than `len`, we have to continue */

   /* Part two */
   ASSERT(rb->read_pos == 0);
   actual_len2 = MIN(len - actual_len, rb->write_pos);
   memcpy(buf + actual_len, rb->buf, actual_len2);
   rb->read_pos += actual_len2;
   rb->elems -= actual_len2;

   return actual_len + actual_len2;
}

bool ringbuf_read_elem(struct ringbuf *rb, void *elem_ptr /* out */)
{
   if (ringbuf_is_empty(rb))
      return false;

   memcpy(elem_ptr, rb->buf + rb->read_pos * rb->elem_size, rb->elem_size);
   rb->read_pos = (rb->read_pos + 1) % rb->max_elems;
   rb->elems--;
   return true;
}

bool ringbuf_unwrite_elem(struct ringbuf *rb, void *elem_ptr /* out */)
{
   if (ringbuf_is_empty(rb))
      return false;

   rb->write_pos = (rb->max_elems + rb->write_pos - 1) % rb->max_elems;
   rb->elems--;

   if (elem_ptr)
      memcpy(elem_ptr, rb->buf + rb->write_pos * rb->elem_size, rb->elem_size);

   return true;
}
