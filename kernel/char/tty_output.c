
#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/color_defs.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/term.h>

#include <termios.h>      // system header

#include "term_int.h"
#include "tty_int.h"

term_write_filter_ctx_t term_write_filter_ctx;

static const u8 fg_csi_to_vga[256] =
{
   [30] = COLOR_BLACK,
   [31] = COLOR_RED,
   [32] = COLOR_GREEN,
   [33] = COLOR_YELLOW,
   [34] = COLOR_BLUE,
   [35] = COLOR_MAGENTA,
   [36] = COLOR_CYAN,
   [37] = COLOR_WHITE,
   [90] = COLOR_BRIGHT_BLACK,
   [91] = COLOR_BRIGHT_RED,
   [92] = COLOR_BRIGHT_GREEN,
   [93] = COLOR_BRIGHT_YELLOW,
   [94] = COLOR_BRIGHT_BLUE,
   [95] = COLOR_BRIGHT_MAGENTA,
   [96] = COLOR_BRIGHT_CYAN,
   [97] = COLOR_BRIGHT_WHITE
};

static void
tty_filter_handle_csi_ABCD(int *params,
                           int pc,
                           char c,
                           term_write_filter_ctx_t *ctx)
{
   int d[4] = {0};
   d[c - 'A'] = MAX(1, params[0]);

   term_action a = {
      .type2 = a_move_ch_and_cur_rel,
      .arg1 = -d[0] + d[1],
      .arg2 =  d[2] - d[3]
   };

   term_execute_action(&a);
}

static void
tty_filter_handle_csi_m_param(int p, u8 *color, term_write_filter_ctx_t *ctx)
{
   u8 tmp;
   u8 fg = get_color_fg(tty_curr_color);
   u8 bg = get_color_bg(tty_curr_color);

   switch(p) {

      case 0:
         /* Reset all attributes */
         fg = DEFAULT_FG_COLOR;
         bg = DEFAULT_BG_COLOR;
         goto set_color;

      case 39:
         /* Reset fg color to the default value */
         fg = DEFAULT_FG_COLOR;
         goto set_color;

      case 49:
         /* Reset bg color to the default value */
         bg = DEFAULT_BG_COLOR;
         goto set_color;


      case 7:
         /* Reverse video */
         tmp = fg;
         fg = bg;
         bg = tmp;
         goto set_color;
   }

   if ((30 <= p && p <= 37) || (90 <= p && p <= 97)) {

      /* Set foreground color */
      fg = fg_csi_to_vga[p];
      goto set_color;

   } else if ((40 <= p && p <= 47) || (100 <= p && p <= 107)) {

      /* Set background color */
      bg = fg_csi_to_vga[p - 10];
      goto set_color;
   }

   return;

set_color:
   tty_curr_color = make_color(fg, bg);
   *color = tty_curr_color;
}

static void
tty_filter_handle_csi_m(int *params,
                        int pc,
                        u8 *color,
                        term_write_filter_ctx_t *ctx)
{
   if (!pc) {
      /*
       * Omitting all params, for example: "ESC[m", is equivalent to
       * having just one parameter set to 0.
       */
      pc = 1;
   }

   for (int i = 0; i < pc; i++) {
      tty_filter_handle_csi_m_param(params[i], color, ctx);
   }
}

static int
tty_filter_end_csi_seq(char c, u8 *color, term_write_filter_ctx_t *ctx)
{
   const char *endptr;
   int params[16] = {0};
   int pc = 0;

   ctx->param_bytes[ctx->pbc] = 0;
   ctx->interm_bytes[ctx->ibc] = 0;
   ctx->state = TERM_WFILTER_STATE_DEFAULT;

   if (ctx->pbc) {

      endptr = ctx->param_bytes - 1;

      do {
         params[pc++] = tilck_strtol(endptr + 1, &endptr, NULL);
      } while (*endptr);

   }

   switch (c) {

      case 'A': // UP    -> move_rel(-param1, 0)
      case 'B': // DOWN  -> move_rel(+param1, 0)
      case 'C': // RIGHT -> move_rel(0, +param1)
      case 'D': // LEFT  -> move_rel(0, -param1)

         tty_filter_handle_csi_ABCD(params, pc, c, ctx);
         break;

      case 'm':
         tty_filter_handle_csi_m(params, pc, color, ctx);
         break;
   }

   ctx->pbc = ctx->ibc = 0;
   return TERM_FILTER_WRITE_BLANK;
}

static int
tty_filter_handle_csi_seq(char c, u8 *color, term_write_filter_ctx_t *ctx)
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
      return tty_filter_end_csi_seq(c, color, ctx);
   }

   /* We shouldn't get here. Something's gone wrong: return the default state */
   ctx->state = TERM_WFILTER_STATE_DEFAULT;
   ctx->pbc = ctx->ibc = 0;
   return TERM_FILTER_WRITE_BLANK;
}

int tty_term_write_filter(char c, u8 *color, void *ctx_arg)
{
   term_write_filter_ctx_t *ctx = ctx_arg;

   if (LIKELY(ctx->state == TERM_WFILTER_STATE_DEFAULT)) {

      switch (c) {

         case '\033':
            ctx->state = TERM_WFILTER_STATE_ESC1;
            return TERM_FILTER_WRITE_BLANK;

         case '\n':

            if (c_term.c_oflag & (OPOST | ONLCR))
               term_internal_write_char2('\r', *color);

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
