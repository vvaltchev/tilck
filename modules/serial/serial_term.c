/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/term.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/safe_ringbuf.h>
#include <tilck/kernel/sync.h>

#include <tilck/mods/serial.h>

struct term_action {

   /* Only one action is supported: write */
   const char *buf;
   size_t len;
};

STATIC_ASSERT(sizeof(struct term_action) == (2 * sizeof(ulong)));

struct sterm {

   bool initialized;
   u16 serial_port_fwd;

   struct kmutex lock;
   struct safe_ringbuf ringb;
   struct term_action actions_buf[32];
};

static struct sterm first_instance;

static enum term_type sterm_get_type(void)
{
   return term_type_serial;
}

static bool
sterm_is_initialized(term *_t)
{
   struct sterm *const t = _t;
   return t->initialized;
}

static void
sterm_get_params(term *_t, struct term_params *out)
{
   *out = (struct term_params) {
      .rows = 25,
      .cols = 80,
      .type = term_type_serial,
      .vi = NULL,
   };
}

static void
sterm_action_write(term *_t, const char *buf, size_t len)
{
   struct sterm *const t = _t;

   for (u32 i = 0; i < len; i++) {

      if (buf[i] == '\n')
         serial_write(t->serial_port_fwd, '\r');

      serial_write(t->serial_port_fwd, buf[i]);
   }
}


/* Handle the _VERY UNLIKELY_ case were `t->actions_buf` is full */
static void
sterm_unable_to_enqueue_action(struct sterm *t,
                               struct term_action a,
                               bool *was_empty)
{
   extern bool __in_printk; /* defined in printk.c */
   struct term_action other_action;
   bool written;

   /* We got here because the ringbuf was full in the first place */
   ASSERT(*was_empty);

   if (__in_printk) {

      if (in_panic()) {

         /* Stop caring about IRQs and stuff: write everything we have */

         while (safe_ringbuf_read_elem(&t->ringb, &other_action))
            sterm_action_write(t, other_action.buf, other_action.len);

         /* Finally, write our action */
         sterm_action_write(t, a.buf, a.len);
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

         kmutex_lock(&t->lock);
         {
            written = safe_ringbuf_write_elem_ex(&t->ringb, &a, was_empty);
         }
         kmutex_unlock(&t->lock);

      } while (!written);
   }
}

static void
serial_term_execute_or_enqueue_action(term *_t, struct term_action a)
{
   struct sterm *const t = _t;
   bool was_empty;

   if (UNLIKELY(!safe_ringbuf_write_elem_ex(&t->ringb, &a, &was_empty)))
      sterm_unable_to_enqueue_action(t, a, &was_empty);

   if (!was_empty)
      return; /* just enqueue the action */

   if (UNLIKELY(in_panic()))
      goto panic_case;

   kmutex_lock(&t->lock);
   {
      while (safe_ringbuf_read_elem(&t->ringb, &a))
         sterm_action_write(t, a.buf, a.len);
   }
   kmutex_unlock(&t->lock);
   return;

panic_case:
   while (safe_ringbuf_read_elem(&t->ringb, &a))
      sterm_action_write(t, a.buf, a.len);
}

static void
sterm_write(term *_t, const char *buf, size_t len, u8 color)
{
   struct sterm *const t = _t;

   struct term_action a = {
      .buf = buf,
      .len = len,
   };

   serial_term_execute_or_enqueue_action(t, a);
}

static term *sterm_get_first_inst(void)
{
   return &first_instance;
}

static term *
alloc_sterm_struct(void)
{
   return kzmalloc(sizeof(struct sterm));
}

static void
free_sterm_struct(term *_t)
{
   struct sterm *const t = _t;
   ASSERT(t != &first_instance);
   kfree2(t, sizeof(struct sterm));
}

static void
dispose_sterm(term *_t)
{
   struct sterm *const t = _t;
   kmutex_destroy(&t->lock);
}

static int
sterm_init(term *_t, u16 serial_port_fwd)
{
   struct sterm *const t = _t;

   t->serial_port_fwd = serial_port_fwd;

   kmutex_init(&t->lock, 0);

   safe_ringbuf_init(&t->ringb,
                     ARRAY_SIZE(t->actions_buf),
                     sizeof(struct term_action),
                     t->actions_buf);

   t->initialized = true;
   return 0;
}

static void sterm_ignored()
{
   /* do nothing */
}

static const struct term_interface intf = {

   .get_type = sterm_get_type,
   .is_initialized = sterm_is_initialized,
   .get_params = sterm_get_params,

   .write = sterm_write,
   .scroll_up = (void*)sterm_ignored,
   .scroll_down = (void*)sterm_ignored,
   .set_col_offset = (void*)sterm_ignored,
   .pause_video_output = (void*)sterm_ignored,
   .restart_video_output = (void*)sterm_ignored,
   .set_filter = (void*)sterm_ignored,

   .get_first_term = sterm_get_first_inst,
   .video_term_init = NULL,
   .serial_term_init = sterm_init,
   .alloc = alloc_sterm_struct,
   .free = free_sterm_struct,
   .dispose = dispose_sterm,
};

__attribute__((constructor))
static void register_term(void)
{
   register_term_intf(&intf);
}
