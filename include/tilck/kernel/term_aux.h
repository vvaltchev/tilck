/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/safe_ringbuf.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/sched.h>

struct term_action;

struct term_rb_data {
   struct kmutex lock;
   struct safe_ringbuf rb;
};

void
init_term_rb_data(struct term_rb_data *d,
                  u16 max_elems,
                  u16 elem_size,
                  void *buf);

void
dispose_term_rb_data(struct term_rb_data *d);

void
term_handle_full_ringbuf(term *t,
                         struct term_rb_data *rb_data,
                         struct term_action *a,
                         bool *was_empty,
                         void (*exec_everything)(term *));

/*
 * C "template" function used to allow code sharing between video term and
 * serial term. The idea is simple: do NOT treat this as a function, but as
 * a macro to put in your term_execute_or_enqueue_action() function. This way,
 * all this code will be inlined in just one place per term implementation.
 * See video_term's term_execute_or_enqueue_action() implementation.
 */

static ALWAYS_INLINE void
term_execute_or_enqueue_action_template(term *t,
                                        struct term_rb_data *rb_data,
                                        struct term_action *a,
                                        void (*exec_everything)(term *))
{
   bool was_empty;

   if (UNLIKELY(!safe_ringbuf_write_elem_ex(&rb_data->rb, a, &was_empty))) {

      term_handle_full_ringbuf(t,
                               rb_data,
                               a,
                               &was_empty,
                               exec_everything);

      /* NOTE: do not return */
   }

   if (!was_empty) {

      /*
       * OK, no matter if we went through term_handle_full_ringbuf() or not,
       * at the moment we enqueued our action the ringbuf was not empty,
       * therefore we won't try to execute the actions. That will happen in the
       * only task that found the ringbuf empty.
       */
      return;
   }

   if (UNLIKELY(in_panic()) || !is_preemption_enabled()) {
      /* We don't need to grab the lock, as the preemption is disabled! */
      return exec_everything(t);
   }

   /* Don't disable the preemption, just grab term's lock */
   kmutex_lock(&rb_data->lock);
   {
      exec_everything(t);
   }
   kmutex_unlock(&rb_data->lock);
}
