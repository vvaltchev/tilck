/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/term.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/mods/serial.h>

struct term {

   bool initialized;
   u16 serial_port_fwd;
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
sterm_read_info(struct term *t, struct tilck_term_info *out)
{
   *out = (struct tilck_term_info) {
      .rows = 25,
      .cols = 80,
      .vi = NULL,
   };
}

static void
sterm_write(struct term *t, const char *buf, size_t len, u8 color)
{
   for (u32 i = 0; i < len; i++) {
      serial_write(t->serial_port_fwd, buf[i]);
   }
}

static void
sterm_scroll_up(struct term *t, u32 lines) { }

static void
sterm_scroll_down(struct term *t, u32 lines) { }

static void
sterm_set_col_offset(struct term *t, u32 off) { }

static void
sterm_pause_video_output(struct term *t) { }

static void
sterm_restart_video_output(struct term *t) { }

static void
sterm_set_cursor_enabled(struct term *t, bool value) { }

static void
sterm_set_filter(struct term *t, term_filter func, void *ctx) { }

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
   return 0;
}

static const struct term_interface intf = {

   .get_type = sterm_get_type,
   .is_initialized = sterm_is_initialized,
   .read_info = sterm_read_info,

   .write = sterm_write,
   .scroll_up = sterm_scroll_up,
   .scroll_down = sterm_scroll_up,
   .set_col_offset = sterm_set_col_offset,
   .pause_video_output = sterm_pause_video_output,
   .restart_video_output = sterm_restart_video_output,
   .set_cursor_enabled = sterm_set_cursor_enabled,
   .set_filter = sterm_set_filter,

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
