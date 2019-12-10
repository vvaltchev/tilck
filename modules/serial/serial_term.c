/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/term.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/safe_ringbuf.h>
#include <tilck/mods/serial.h>

struct term_action {

   /* Only one action is supported: write */
   const char *buf;
   size_t len;
};

STATIC_ASSERT(sizeof(struct term_action) == (2 * sizeof(uptr)));

struct term {

   bool initialized;
   u16 serial_port_fwd;

   struct safe_ringbuf ringb;
   struct term_action actions_buf[32];
};

static struct term first_instance;

static enum term_type sterm_get_type(void)
{
   return term_type_serial;
}

static bool
sterm_is_initialized(struct term *t)
{
   return t->initialized;
}

static void
sterm_get_params(struct term *t, struct term_params *out)
{
   *out = (struct term_params) {
      .rows = 25,
      .cols = 80,
      .vi = NULL,
   };
}

static void
sterm_action_write(struct term *t, const char *buf, size_t len)
{
   for (u32 i = 0; i < len; i++) {
      serial_write(t->serial_port_fwd, buf[i]);
   }
}

static void
serial_term_execute_or_enqueue_action(struct term *t, struct term_action a)
{
   bool written;
   bool was_empty;

   written = safe_ringbuf_write_elem_ex(&t->ringb, &a, &was_empty);

   /*
    * written would be false only if the ringbuf was full. In order that to
    * happen, we'll need ARRAY_SIZE(actions_buf) nested interrupts and
    * all of them need to issue a term_* call. Virtually "impossible".
    */
   VERIFY(written);

   if (was_empty) {

      while (safe_ringbuf_read_elem(&t->ringb, &a))
         sterm_action_write(t, a.buf, a.len);
   }
}

static void
sterm_write(struct term *t, const char *buf, size_t len, u8 color)
{
   struct term_action a = {
      .buf = buf,
      .len = len,
   };

   serial_term_execute_or_enqueue_action(t, a);
}

static struct term *sterm_get_first_inst(void)
{
   return &first_instance;
}

static struct term *
alloc_sterm_struct(void)
{
   return kzmalloc(sizeof(struct term));
}

static void
free_sterm_struct(struct term *t)
{
   ASSERT(t != &first_instance);
   kfree2(t, sizeof(struct term));
}

static void
dispose_sterm(struct term *t)
{
   /* Do nothing */
}

static int
sterm_init(struct term *t, u16 serial_port_fwd)
{
   t->serial_port_fwd = serial_port_fwd;
   t->initialized = true;

   safe_ringbuf_init(&t->ringb,
                     ARRAY_SIZE(t->actions_buf),
                     sizeof(struct term_action),
                     t->actions_buf);
   return 0;
}

static const struct term_interface intf = {

   .get_type = sterm_get_type,
   .is_initialized = sterm_is_initialized,
   .get_params = sterm_get_params,

   .write = sterm_write,
   .scroll_up = NULL,
   .scroll_down = NULL,
   .set_col_offset = NULL,
   .pause_video_output = NULL,
   .restart_video_output = NULL,
   .set_filter = NULL,

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
