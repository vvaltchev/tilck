/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/color_defs.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/cmdline.h>

#include <termios.h>      // system header

#include "term_int.h"
#include "tty_int.h"
#include "tty_output_default_state.c.h"

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
tty_filter_handle_csi_ABCD(u32 *params,
                           int pc,
                           u8 c,
                           term_action *a,
                           term_write_filter_ctx_t *ctx)
{
   u32 d[4] = {0};
   d[c - 'A'] = MAX(1u, params[0]);

   *a = (term_action) {
      .type2 = a_move_ch_and_cur_rel,
      .arg1 = -d[0] + d[1],
      .arg2 =  d[2] - d[3]
   };
}

static void
tty_filter_handle_csi_m_param(u32 p, u8 *color, term_write_filter_ctx_t *ctx)
{
   tty *const t = ctx->t;

   u8 tmp;
   u8 fg = get_color_fg(t->curr_color);
   u8 bg = get_color_bg(t->curr_color);

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
   t->curr_color = make_color(fg, bg);
   *color = t->curr_color;
}

static void
tty_filter_handle_csi_m(u32 *params,
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

static inline void
tty_move_cursor_begin_nth_row(tty *t, term_action *a, u32 row)
{
   u32 new_row =
      MIN(term_get_curr_row(t->term_inst) + row,
          term_get_rows(t->term_inst) - 1u);

   *a = (term_action) {
      .type2 = a_move_ch_and_cur,
      .arg1 = new_row,
      .arg2 = 0
   };
}

static enum term_fret
tty_filter_end_csi_seq(u8 c,
                       u8 *color,
                       term_action *a,
                       term_write_filter_ctx_t *ctx)
{
   const char *endptr;
   u32 params[16] = {0};
   int pc = 0;
   tty *const t = ctx->t;

   ctx->param_bytes[ctx->pbc] = 0;
   ctx->interm_bytes[ctx->ibc] = 0;
   ctx->state = TERM_WFILTER_STATE_DEFAULT;

   if (ctx->pbc) {

      endptr = ctx->param_bytes - 1;

      do {
         params[pc++] = (u32) tilck_strtol(endptr + 1, &endptr, NULL);
      } while (*endptr);

   }

   switch (c) {

      case 'A': // UP    -> move_rel(-param1, 0)
      case 'B': // DOWN  -> move_rel(+param1, 0)
      case 'C': // RIGHT -> move_rel(0, +param1)
      case 'D': // LEFT  -> move_rel(0, -param1)

         tty_filter_handle_csi_ABCD(params, pc, c, a, ctx);
         break;

      case 'm': /* SGR (Select Graphic Rendition) parameters */
         tty_filter_handle_csi_m(params, pc, color, ctx);
         break;

      case 'E':
         /* Move the cursor 'n' lines down and set col = 0 */
        tty_move_cursor_begin_nth_row(t, a, MAX(1u, params[0]));
        break;

      case 'F':
         /* Move the cursor 'n' lines up and set col = 0 */
         tty_move_cursor_begin_nth_row(t, a, -MAX(1u, params[0]));
         break;

      case 'G':
         /* Move the cursor to the column 'n' (absolute, 1-based) */
         params[0] = MAX(1u, params[0]) - 1;

         *a = (term_action) {
            .type2 = a_move_ch_and_cur,
            .arg1 = term_get_curr_row(t->term_inst),
            .arg2 = MIN((u32)params[0], term_get_cols(t->term_inst) - 1u)
         };

         break;

      case 'f':
      case 'H':
         /* Move the cursor to (n, m) (absolute, 1-based) */
         params[0] = MAX(1u, params[0]) - 1;
         params[1] = MAX(1u, params[1]) - 1;

         *a = (term_action) {
            .type2 = a_move_ch_and_cur,
            .arg1 = UNSAFE_MIN((u32)params[0], term_get_rows(t->term_inst)-1u),
            .arg2 = UNSAFE_MIN((u32)params[1], term_get_cols(t->term_inst)-1u)
         };

         break;

      case 'J':
         *a = (term_action) { .type1 = a_erase_in_display, .arg = params[0] };
         break;

      case 'K':
         *a = (term_action) { .type1 = a_erase_in_line, .arg = params[0] };
         break;

      case 'S':
         *a = (term_action) {
            .type1 = a_non_buf_scroll_up,
            .arg = UNSAFE_MAX(1, params[0])
         };
         break;

      case 'T':
         *a = (term_action) {
            .type1 = a_non_buf_scroll_down,
            .arg = UNSAFE_MAX(1, params[0])
         };
         break;

      case 'n':

         if (params[0] == 6) {

            /* DSR (Device Status Report) */

            char dsr[16];
            snprintk(dsr, sizeof(dsr), "\033[%u;%uR",
                     term_get_curr_row(t->term_inst) + 1,
                     term_get_curr_col(t->term_inst) + 1);

            for (u8 *p = (u8 *)dsr; *p; p++)
               tty_keypress_handler_int(ctx->t, *p, *p, false);
         }

         break;

      case 's':
         /* SCP (Save Cursor Position) */
         ctx->t->saved_cur_row = term_get_curr_row(t->term_inst);
         ctx->t->saved_cur_col = term_get_curr_col(t->term_inst);
         break;

      case 'u':
         /* RCP (Restore Cursor Position) */
         *a = (term_action) {
            .type2 = a_move_ch_and_cur,
            .arg1 = ctx->t->saved_cur_row,
            .arg2 = ctx->t->saved_cur_col
         };
         break;

      case 'd':
         /* VPA: Move cursor to the indicated row, current column */
         params[0] = MAX(1u, params[0]) - 1;

         *a = (term_action) {
            .type2 = a_move_ch_and_cur,
            .arg1 = UNSAFE_MIN((u32)params[0], term_get_rows(t->term_inst)-1u),
            .arg2 = term_get_curr_col(t->term_inst)
         };
         break;
   }

   ctx->pbc = ctx->ibc = 0;
   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_handle_csi_seq(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   term_write_filter_ctx_t *ctx = ctx_arg;

   if (0x30 <= *c && *c <= 0x3F) {

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

      ctx->param_bytes[ctx->pbc++] = (char) *c;
      return TERM_FILTER_WRITE_BLANK;
   }

   if (0x20 <= *c && *c <= 0x2F) {

      /* This is an "intermediate" byte */

      if (ctx->ibc >= ARRAY_SIZE(ctx->interm_bytes)) {
         ctx->ibc = 0;
         ctx->state = TERM_WFILTER_STATE_DEFAULT;
         return TERM_FILTER_WRITE_BLANK;
      }

      ctx->interm_bytes[ctx->ibc++] = (char) *c;
      return TERM_FILTER_WRITE_BLANK;
   }

   if (0x40 <= *c && *c <= 0x7E) {
      /* Final CSI byte */
      return tty_filter_end_csi_seq(*c, color, a, ctx);
   }

   /* We shouldn't get here. Something's gone wrong: return the default state */
   ctx->state = TERM_WFILTER_STATE_DEFAULT;
   ctx->pbc = ctx->ibc = 0;
   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_handle_unknown_esc_seq(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   term_write_filter_ctx_t *ctx = ctx_arg;

   if (0x40 <= *c && *c <= 0x5f) {
      /* End of any possible (unknown) escape sequence */
      ctx->state = TERM_WFILTER_STATE_DEFAULT;
   }

   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_handle_state_esc1(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   term_write_filter_ctx_t *ctx = ctx_arg;

   switch (*c) {

      case '[':
         ctx->state = TERM_WFILTER_STATE_ESC2_CSI;
         ctx->pbc = ctx->ibc = 0;
         break;

      case 'c':
         {
            *a = (term_action) { .type1 = a_reset };
            ctx->state = TERM_WFILTER_STATE_DEFAULT;
         }
         break;

      case '(':
      case ')':
         /*
          * HACK: In theory, a console should have two character sets, G0 and
          * and G1 which can be switched with shift-in/shift-out. Both those
          * character sets have (at most) 4 translation tables.
          * In other to make G0 use the translation table 0, 1, 2, 3:
          *    "ESC ( B", "ESC ( 0", "ESC ( U", "ESC ( K"
          * In order to make the same change for G1:
          *    "ESC ) B", "ESC ) 0", "ESC ) U", "ESC ) K"
          *
          * Since currently Tilck has just a single character set, shift in
          * and shift out just change its translation table. That's why here
          * we have to deal with '(' and ')' in the same way.
          *
          * TODO: make Tilck's console to support 2 character sets.
          */
         ctx->state = TERM_WFILTER_STATE_ESC2_PAR;
         break;

      default:
         ctx->state = TERM_WFILTER_STATE_ESC2_UNKNOWN;
         /*
          * We need to handle now this case because the sequence might be 1-char
          * long, like "^[_". Therefore, if the current character is in the
          * [0x40, 0x5f] range, we have to go back to the default state.
          * Otherwise, this is the identifier of a more complex sequence which
          * will end with the first character in the range [0x40, 0x5f].
          */

         return tty_handle_unknown_esc_seq(c, color, a, ctx);
   }

   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_handle_state_esc2_par(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   term_write_filter_ctx_t *ctx = ctx_arg;

   switch (*c) {

      case '0':
         ctx->use_alt_charset = true;
         break;

      case 'B':
         ctx->use_alt_charset = false;
         break;

      default:
         /* do nothing */
         break;
   }

   ctx->state = TERM_WFILTER_STATE_DEFAULT;
   return TERM_FILTER_WRITE_BLANK;
}

enum term_fret
tty_term_write_filter(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   static const term_filter table[] =
   {
      [TERM_WFILTER_STATE_DEFAULT] = &tty_handle_default_state,
      [TERM_WFILTER_STATE_ESC1] = &tty_handle_state_esc1,
      [TERM_WFILTER_STATE_ESC2_PAR] = &tty_handle_state_esc2_par,
      [TERM_WFILTER_STATE_ESC2_CSI] = &tty_handle_csi_seq,
      [TERM_WFILTER_STATE_ESC2_UNKNOWN] = &tty_handle_unknown_esc_seq
   };

   term_write_filter_ctx_t *ctx = ctx_arg;

   if (kopt_serial_mode == TERM_SERIAL_CONSOLE)
      return TERM_FILTER_WRITE_C;

   ASSERT(ctx->state < ARRAY_SIZE(table));
   return table[ctx->state](c, color, a, ctx);
}

ssize_t tty_write_int(tty *t, devfs_file_handle *h, char *buf, size_t size)
{
   /* term_write's size is limited to 2^20 - 1 */
   size = MIN(size, (size_t)MB - 1);
   term_write(t->term_inst, buf, size, t->curr_color);
   return (ssize_t) size;
}
