
#include <exos/common/basic_defs.h>
#include <exos/common/string_util.h>

#include <exos/kernel/fs/exvfs.h>
#include <exos/kernel/fs/devfs.h>
#include <exos/kernel/term.h>

#include <termios.h>      // system header

#include "term_int.h"
#include "tty_int.h"

term_write_filter_ctx_t term_write_filter_ctx;

static int
tty_filter_handle_csi_seq(char c, u8 color, term_write_filter_ctx_t *ctx)
{
   if (0x30 <= c && c <= 0x3F) {

      /* This is a parameter byte */

      if (ctx->pbc >= ARRAY_SIZE(ctx->param_bytes)) {

         /*
          * The param bytes exceed our limits, something gone wrong: just return
          * back to the default state ignoring this escape sequence.
          */

         ctx->pbc = 0;
         ctx->state = TERM_WFILTER_STATE_DEFAULT;
         return TERM_FILTER_WRITE_BLANK;
      }

      ctx->param_bytes[ctx->pbc++] = c;
      return TERM_FILTER_WRITE_BLANK;
   }

   if (0x20 <= c && c <= 0x2F) {

      /* This is an "intermediate" byte */

      if (ctx->ibc >= ARRAY_SIZE(ctx->interm_bytes)) {
         ctx->ibc = 0;
         ctx->state = TERM_WFILTER_STATE_DEFAULT;
         return TERM_FILTER_WRITE_BLANK;
      }

      ctx->interm_bytes[ctx->ibc++] = c;
      return TERM_FILTER_WRITE_BLANK;
   }

   if (0x40 <= c && c <= 0x7E) {

      /* Final CSI byte */

      ctx->param_bytes[ctx->pbc] = 0;
      ctx->interm_bytes[ctx->ibc] = 0;
      ctx->state = TERM_WFILTER_STATE_DEFAULT;


      const char *endptr;
      int param1 = 0;

      if (ctx->pbc) {
         param1 = exos_strtol(ctx->param_bytes, &endptr, NULL);

         // NOTE: param2 is unused at the moment
         // if (*endptr == ';')
         //    param2 = exos_strtol(endptr + 1, &endptr, NULL);
      }

      switch (c) {

         case 'A': // UP    -> move_rel(-param1, 0)
         case 'B': // DOWN  -> move_rel(+param1, 0)
         case 'C': // RIGHT -> move_rel(0, +param1)
         case 'D': // LEFT  -> move_rel(0, -param1)

            {
               int d[4] = {0};
               d[c - 'A'] = MAX(1, param1);

               term_action a = {
                  .type2 = a_move_ch_and_cur_rel,
                  .arg1 = -d[0] + d[1],
                  .arg2 =  d[2] - d[3]
               };

               term_execute_action(&a);
               break;
            }

         case 'm':
            //printk("M: '%s'\n", ctx->param_bytes);

            // TODO: handle the 'm' command (set color)
            break;
      }

      ctx->pbc = ctx->ibc = 0;
      return TERM_FILTER_WRITE_BLANK;
   }

   /* We shouldn't get here. Something's gone wrong: return the default state */
   ctx->state = TERM_WFILTER_STATE_DEFAULT;
   ctx->pbc = ctx->ibc = 0;
   return TERM_FILTER_WRITE_BLANK;
}

int tty_term_write_filter(char c, u8 color, void *ctx_arg)
{
   term_write_filter_ctx_t *ctx = ctx_arg;

   if (LIKELY(ctx->state == TERM_WFILTER_STATE_DEFAULT)) {

      switch (c) {

         case '\033':
            ctx->state = TERM_WFILTER_STATE_ESC1;
            return TERM_FILTER_WRITE_BLANK;

         case '\n':

            if (c_term.c_oflag & (OPOST | ONLCR))
               term_internal_write_char2('\r', color);

            break;

         case '\a':
         case '\f':
         case '\v':
            /* Ignore some characters */
            return TERM_FILTER_WRITE_BLANK;
      }

      return TERM_FILTER_WRITE_C;
   }

   switch (ctx->state) {

      case TERM_WFILTER_STATE_ESC1:

         switch (c) {

            case '[':
               ctx->state = TERM_WFILTER_STATE_ESC2;
               ctx->pbc = ctx->ibc = 0;
               break;

            case 'c':
               // TODO: support the RIS (reset to initial state) command

            default:
               ctx->state = TERM_WFILTER_STATE_DEFAULT;
         }

         return TERM_FILTER_WRITE_BLANK;

      case TERM_WFILTER_STATE_ESC2:
         return tty_filter_handle_csi_seq(c, color, ctx);

      default:
         NOT_REACHED();
   }
}
