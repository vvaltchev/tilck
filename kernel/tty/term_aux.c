/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/term.h>
#include <tilck/kernel/term_aux.h>

void
term_handle_full_ringbuf(term *t,
                         struct term_rb_data *rb_data,
                         struct term_action *a,
                         bool *was_empty,
                         void (*exec_everything)(void *))
{
   extern bool __in_printk; /* defined in printk.c */
   bool written;

   /* We got here because the ringbuf was full in the first place */
   ASSERT(!*was_empty);

   if (__in_printk) {

      if (in_panic()) {

         /* Stop caring about IRQs and stuff: execute everything */
         exec_everything(t);
         return;
      }

      /*
       * OK, this is pretty weird: it's maybe (?) the absurd case of many nested
       * IRQs each one of them calling printk(), which at some point, finished
       * its own ring buffer and ended up flushing directly the messages to
       * this layer. The whole thing is more theoretical than practical: it
       * should never happen, but if it does, it's better to not pass unnoticed.
       */

      panic("Term ringbuf full while in printk()");

   } else {

     /*
      * We CANNOT possibly be in an IRQ context: we're likely in tty_write()
      * and we can be preempted.
      */

      do {

         kmutex_lock(&rb_data->lock);
         {
            written = safe_ringbuf_write_elem_ex(&rb_data->rb, a, was_empty);
         }
         kmutex_unlock(&rb_data->lock);

      } while (!written);
   }
}

void
init_term_rb_data(struct term_rb_data *d,
                  u16 max_elems,
                  u16 elem_size,
                  void *buf)
{
   kmutex_init(&d->lock, 0);
   safe_ringbuf_init(&d->rb, max_elems, elem_size, buf);
}

void
dispose_term_rb_data(struct term_rb_data *d)
{
   safe_ringbuf_destory(&d->rb);
   kmutex_destroy(&d->lock);
}
