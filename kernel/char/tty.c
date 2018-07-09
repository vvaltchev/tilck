
#include <exos/common/basic_defs.h>
#include <exos/common/string_util.h>

#include <exos/kernel/fs/exvfs.h>
#include <exos/kernel/fs/devfs.h>
#include <exos/kernel/errno.h>
#include <exos/kernel/kmalloc.h>
#include <exos/kernel/sync.h>
#include <exos/kernel/kb.h>
#include <exos/kernel/process.h>
#include <exos/kernel/term.h>
#include <exos/kernel/user.h>
#include <exos/kernel/ringbuf.h>
#include <exos/kernel/kb_scancode_set1_keys.h>

#include <termios.h>      // system header
#include "tty_input.c.h"

void tty_update_special_ctrl_handlers(void);

typedef enum {

   TERM_WFILTER_STATE_DEFAULT,
   TERM_WFILTER_STATE_ESC1,
   TERM_WFILTER_STATE_ESC2

} term_write_filter_state_t;

typedef struct {

   term_write_filter_state_t state;
   char param_bytes[16];
   char interm_bytes[16];

   u8 pbc; /* param bytes count */
   u8 ibc; /* intermediate bytes count */

} term_write_filter_ctx_t;

static term_write_filter_ctx_t term_write_filter_ctx;

static int
tty_term_write_filter(char *c,
                      u8 *color,
                      term_int_write_char_func write_char_func,
                      void *ctx_arg)
{
   term_write_filter_ctx_t *ctx = ctx_arg;

   switch (ctx->state) {

      case TERM_WFILTER_STATE_DEFAULT:
         goto default_state;

      case TERM_WFILTER_STATE_ESC1:
         goto begin_esc_seq;

      case TERM_WFILTER_STATE_ESC2:
         goto csi_seq;

      default:
         NOT_REACHED();
   }

default_state:

   switch (*c) {

      case '\033':
         ctx->state = TERM_WFILTER_STATE_ESC1;
         return TERM_FILTER_FUNC_RET_BLANK;

      case '\n':

         if (c_term.c_oflag & (OPOST | ONLCR))
            write_char_func('\r', *color);

         break;

      case '\a':
      case '\f':
      case '\v':
         /* Ignore some characters */
         return TERM_FILTER_FUNC_RET_BLANK;

   }

   return TERM_FILTER_FUNC_RET_WRITE_C;

csi_seq:

   if (0x30 <= *c && *c <= 0x3F) {

      /* This is a parameter byte */

      if (ctx->pbc >= ARRAY_SIZE(ctx->param_bytes)) {

         /*
          * The param bytes exceed our limits, something gone wrong: just return
          * back to the default state ignoring this escape sequence.
          */

         ctx->pbc = 0;
         ctx->state = TERM_WFILTER_STATE_DEFAULT;
         return TERM_FILTER_FUNC_RET_BLANK;
      }

      ctx->param_bytes[ctx->pbc++] = *c;
      return TERM_FILTER_FUNC_RET_BLANK;
   }

   if (0x20 <= *c && *c <= 0x2F) {

      /* This is an "intermediate" byte */

      if (ctx->ibc >= ARRAY_SIZE(ctx->interm_bytes)) {
         ctx->ibc = 0;
         ctx->state = TERM_WFILTER_STATE_DEFAULT;
         return TERM_FILTER_FUNC_RET_BLANK;
      }

      ctx->interm_bytes[ctx->ibc++] = *c;
      return TERM_FILTER_FUNC_RET_BLANK;
   }

   if (0x40 <= *c && *c <= 0x7E) {

      /* Final CSI byte */

      ctx->param_bytes[ctx->pbc] = 0;
      ctx->interm_bytes[ctx->ibc] = 0;
      ctx->state = TERM_WFILTER_STATE_DEFAULT;


      const char *endptr;
      int param1 = 0, param2 = 0;

      if (ctx->pbc) {
         param1 = exos_strtol(ctx->param_bytes, &endptr, NULL);

         if (*endptr == ';') {
            param2 = exos_strtol(endptr + 1, &endptr, NULL);
            (void)param2;
         }
      }

      // printk("term seq: '%s', '%s', %c\n",
      //        ctx->param_bytes, ctx->interm_bytes, *c);
      // printk("param1: %d, param2: %d\n", param1, param2);

      switch (*c) {

         case 'A':
            term_move_ch_and_cur_rel(param1, 0);
            break;

         case 'B':
            term_move_ch_and_cur_rel(-param1, 0);
            break;

         case 'C':
            term_move_ch_and_cur_rel(0, param1);
            break;

         case 'D':
            term_move_ch_and_cur_rel(0, -param1);
            break;
      }

      ctx->pbc = ctx->ibc = 0;
      return TERM_FILTER_FUNC_RET_BLANK;
   }

   /* We shouldn't get here. Something's gone wrong: return the default state */
   ctx->state = TERM_WFILTER_STATE_DEFAULT;
   ctx->pbc = ctx->ibc = 0;
   return TERM_FILTER_FUNC_RET_BLANK;

begin_esc_seq:

   switch (*c) {

      case '[':
         ctx->state = TERM_WFILTER_STATE_ESC2;
         ctx->pbc = ctx->ibc = 0;
         break;

      case 'c':
         // TODO: support the RIS (reset to initial state) command

      default:
          ctx->state = TERM_WFILTER_STATE_DEFAULT;
   }

   return TERM_FILTER_FUNC_RET_BLANK;
}

static ssize_t tty_write(fs_handle h, char *buf, size_t size)
{
   // NOTE: the 'size' arg passed to term_write cannot be bigger than 1 MB.
   // TODO: call term_write() in a loop in order to handle size > 1 MB.

   term_write(buf, size);
   return size;
}

/* ----------------- Driver interface ---------------- */

int tty_ioctl(fs_handle h, uptr request, void *argp);

static int tty_create_device_file(int minor, file_ops *ops, devfs_entry_type *t)
{
   *t = DEVFS_CHAR_DEVICE;

   bzero(ops, sizeof(file_ops));

   ops->read = tty_read;
   ops->write = tty_write;
   ops->ioctl = tty_ioctl;
   ops->seek = NULL; /* seek() support is NOT mandatory, of course */
   return 0;
}

void init_tty(void)
{
   c_term = default_termios;
   driver_info *di = kmalloc(sizeof(driver_info));
   di->name = "tty";
   di->create_dev_file = tty_create_device_file;
   int major = register_driver(di);
   int rc = create_dev_file("tty", major, 0 /* minor */);

   if (rc != 0)
      panic("TTY: unable to create /dev/tty (error: %d)", rc);

   kcond_init(&kb_input_cond);
   ringbuf_init(&kb_input_ringbuf, KB_INPUT_BS, 1, kb_input_buf);

   tty_update_special_ctrl_handlers();

   if (kb_register_keypress_handler(&tty_keypress_handler) < 0)
      panic("TTY: unable to register keypress handler");

   term_set_filter_func(tty_term_write_filter, &term_write_filter_ctx);
}
