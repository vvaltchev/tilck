/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/color_defs.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/fs/devfs.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/tty_struct.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/kmalloc.h>

#include "console_int.h"
#include "console_def_state.c.h"

void *alloc_console_data(void)
{
   struct console_data *cd;

   if (!(cd = kzmalloc(sizeof(struct console_data))))
      return NULL;

   if (!(cd->def_state_funcs = kzmalloc(256 * sizeof(term_filter)))) {
      kfree2(cd, sizeof(struct console_data));
      return NULL;
   }

   return cd;
}

void free_console_data(void *data)
{
   if (data) {
      struct console_data *cd = data;
      kfree2(cd->def_state_funcs, 256 * sizeof(term_filter));
      kfree2(data, sizeof(struct console_data));
   }
}

void reset_console_data(struct tty *t)
{
   struct console_data *const cd = t->console_data;

   cd->user_color = t->curr_color;
   cd->c_set = 0;
   cd->c_sets_tables[0] = tty_default_trans_table;
   cd->c_sets_tables[1] = tty_gfx_trans_table;
   cd->filter_ctx.t = t;
   cd->filter_ctx.cd = cd;
}

void tty_reset_filter_ctx(struct tty *t)
{
   struct console_data *cd = t->console_data;

   if (!cd)
      return; /* serial tty */

   struct twfilter_ctx_t *ctx = &cd->filter_ctx;
   ctx->pbc = ctx->ibc = 0;
   ctx->t = t;
   ctx->cd = cd;
   tty_set_state(ctx, &tty_state_default);
}

static void
tty_filter_handle_csi_ABCD(u32 *params,
                           int pc,
                           u8 c,
                           u8 *color,
                           struct term_action *a,
                           struct twfilter_ctx_t *ctx)
{
   int d[4] = {0};
   d[c - 'A'] = (int) MAX(1u, params[0]);

   *a = (struct term_action) {
      .type2 = a_move_ch_and_cur_rel,
      .arg1 = LO_BITS((u32)(-d[0] + d[1]), 8, u32),
      .arg2 = LO_BITS((u32)( d[2] - d[3]), 8, u32),
   };
}

static void
tty_filter_handle_csi_m_param(u32 p, u8 *color, struct twfilter_ctx_t *ctx)
{
   struct tty *const t = ctx->t;
   struct console_data *const cd = ctx->cd;

   u8 fg = get_color_fg(cd->user_color);
   u8 bg = get_color_bg(cd->user_color);

   switch(p) {

      case 0:
         /* Reset all attributes */
         fg = DEFAULT_FG_COLOR;
         bg = DEFAULT_BG_COLOR;
         cd->attrs = 0;
         goto set_color;

      case 39:
         /* Reset fg color to the default value */
         fg = DEFAULT_FG_COLOR;
         goto set_color;

      case 49:
         /* Reset bg color to the default value */
         bg = DEFAULT_BG_COLOR;
         goto set_color;

      case 1:
         cd->attrs |= TTY_ATTR_BOLD;
         goto set_color;

      case 7:
         cd->attrs |= TTY_ATTR_REVERSE;
         goto set_color;

      default:
         /* fall-through */
         break;
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
   cd->user_color = make_color(fg, bg);

   if ((cd->attrs & TTY_ATTR_BOLD) && fg <= 7)
      fg += 8;

   if (cd->attrs & TTY_ATTR_REVERSE)
      t->curr_color = make_color(bg, fg);
   else
      t->curr_color = make_color(fg, bg);

   *color = t->curr_color;
}

static void
tty_filter_handle_csi_m(u32 *params,
                        int pc,
                        u8 c,
                        u8 *color,
                        struct term_action *a,
                        struct twfilter_ctx_t *ctx)
{
   if (!pc) {
      /*
       * Omitting all params, for example: "ESC[m", is equivalent to
       * having just one parameter set to 0.
       */
      params[pc++] = 0;
   }

   for (int i = 0; i < pc; i++) {
      tty_filter_handle_csi_m_param(params[i], color, ctx);
   }
}

static inline void
tty_move_cursor_begin_nth_row(struct tty *t, struct term_action *a, u32 row)
{
   const u32 new_row = MIN(
      term_get_curr_row(t->tstate) + row, t->tparams.rows - 1u
   );

   *a = (struct term_action) {
      .type2 = a_move_ch_and_cur,
      .arg1 = new_row,
      .arg2 = 0,
   };
}

static void
tty_csi_EF_handler(u32 *params,
                   int pc,
                   u8 c,
                   u8 *color,
                   struct term_action *a,
                   struct twfilter_ctx_t *ctx)
{
   struct tty *const t = ctx->t;
   ASSERT(c == 'E' || c == 'F');

   if (c == 'E') {

      /* Move the cursor 'n' lines down and set col = 0 */
      tty_move_cursor_begin_nth_row(t, a, MAX(1u, params[0]));

   } else {

      /* Move the cursor 'n' lines up and set col = 0 */
      tty_move_cursor_begin_nth_row(t, a, -MAX(1u, params[0]));
   }
}

static void
tty_csi_G_handler(u32 *params,
                  int pc,
                  u8 c,
                  u8 *color,
                  struct term_action *a,
                  struct twfilter_ctx_t *ctx)
{
   struct tty *const t = ctx->t;

   /* Move the cursor to the column 'n' (absolute, 1-based) */
   params[0] = MAX(1u, params[0]) - 1;

   *a = (struct term_action) {
      .type2 = a_move_ch_and_cur,
      .arg1 = term_get_curr_row(t->tstate),
      .arg2 = MIN((u32)params[0], t->tparams.cols - 1u),
   };
}

static void
tty_csi_fH_handler(u32 *params,
                   int pc,
                   u8 c,
                   u8 *color,
                   struct term_action *a,
                   struct twfilter_ctx_t *ctx)
{
   struct tty *const t = ctx->t;

   /* Move the cursor to (n, m) (absolute, 1-based) */
   params[0] = MAX(1u, params[0]) - 1;
   params[1] = MAX(1u, params[1]) - 1;

   *a = (struct term_action) {
      .type2 = a_move_ch_and_cur,
      .arg1 = UNSAFE_MIN((u32)params[0], t->tparams.rows - 1u),
      .arg2 = UNSAFE_MIN((u32)params[1], t->tparams.cols - 1u),
   };
}

static void
tty_csi_J_handler(u32 *params,
                  int pc,
                  u8 c,
                  u8 *color,
                  struct term_action *a,
                  struct twfilter_ctx_t *ctx)
{
   *a = (struct term_action) {
      .type2 = a_del_generic,
      .arg1 = TERM_DEL_ERASE_IN_DISPLAY,
      .arg2 = params[0],
   };
}

static void
tty_csi_K_handler(u32 *params,
                  int pc,
                  u8 c,
                  u8 *color,
                  struct term_action *a,
                  struct twfilter_ctx_t *ctx)
{
   *a = (struct term_action) {
      .type2 = a_del_generic,
      .arg1 = TERM_DEL_ERASE_IN_LINE,
      .arg2 = params[0],
   };
}

static void
tty_csi_S_handler(u32 *params,
                  int pc,
                  u8 c,
                  u8 *color,
                  struct term_action *a,
                  struct twfilter_ctx_t *ctx)
{
   *a = (struct term_action) {
      .type2 = a_non_buf_scroll,
      .arg1 = UNSAFE_MAX(1, params[0]),
      .arg2 = non_buf_scroll_up,
   };
}

static void
tty_csi_T_handler(u32 *params,
                  int pc,
                  u8 c,
                  u8 *color,
                  struct term_action *a,
                  struct twfilter_ctx_t *ctx)
{
   *a = (struct term_action) {
      .type2 = a_non_buf_scroll,
      .arg1 = UNSAFE_MAX(1, params[0]),
      .arg2 = non_buf_scroll_down,
   };
}

static void
tty_csi_n_handler(u32 *params,
                  int pc,
                  u8 c,
                  u8 *color,
                  struct term_action *a,
                  struct twfilter_ctx_t *ctx)
{
   struct tty *const t = ctx->t;

   if (params[0] == 6) {

      /* DSR (Device Status Report) */

      char dsr[16];
      snprintk(dsr, sizeof(dsr), "\033[%u;%uR",
               term_get_curr_row(t->tstate) + 1,
               term_get_curr_col(t->tstate) + 1);

      for (char *p = dsr; *p; p++) {
         tty_send_keyevent(t, make_key_event((u32) *p, *p, true));
      }
   }
}

static void
tty_csi_s_handler(u32 *params,
                  int pc,
                  u8 c,
                  u8 *color,
                  struct term_action *a,
                  struct twfilter_ctx_t *ctx)
{
   struct tty *const t = ctx->t;

   /* SCP (Save Cursor Position) */
   ctx->cd->saved_cur_row = term_get_curr_row(t->tstate);
   ctx->cd->saved_cur_col = term_get_curr_col(t->tstate);
}

static void
tty_csi_u_handler(u32 *params,
                  int pc,
                  u8 c,
                  u8 *color,
                  struct term_action *a,
                  struct twfilter_ctx_t *ctx)
{
   struct console_data *const cd = ctx->cd;

   /* RCP (Restore Cursor Position) */
   *a = (struct term_action) {
      .type2 = a_move_ch_and_cur,
      .arg1 = cd->saved_cur_row,
      .arg2 = cd->saved_cur_col,
   };
}

static void
tty_csi_d_handler(u32 *params,
                  int pc,
                  u8 c,
                  u8 *color,
                  struct term_action *a,
                  struct twfilter_ctx_t *ctx)
{
   struct tty *const t = ctx->t;

   /* VPA: Move cursor to the indicated row, current column */
   params[0] = MAX(1u, params[0]) - 1;

   *a = (struct term_action) {
      .type2 = a_move_ch_and_cur,
      .arg1 = UNSAFE_MIN((u32)params[0], t->tparams.rows - 1u),
      .arg2 = term_get_curr_col(t->tstate),
   };
}

static void
tty_csi_hpa_handler(u32 *params,
                    int pc,
                    u8 c,
                    u8 *color,
                    struct term_action *a,
                    struct twfilter_ctx_t *ctx)
{
   struct tty *const t = ctx->t;

   /* HPA: Move cursor to the indicated column, current row */
   params[0] = MAX(1u, params[0]) - 1;

   *a = (struct term_action) {
      .type2 = a_move_ch_and_cur,
      .arg1 = term_get_curr_row(t->tstate),
      .arg2 = UNSAFE_MIN((u32)params[0], t->tparams.cols - 1u),
   };
}

static void
tty_csi_pvt_ext_handler(u32 *params,
                        int pc,
                        u8 c,
                        u8 *color,
                        struct term_action *a,
                        struct twfilter_ctx_t *ctx)
{
   switch (params[0]) {

      case 25:
         if (c == 'h') {

            /* Show the cursor */

            *a = (struct term_action) {
               .type1 = a_enable_cursor,
               .arg = true,
            };

         } else if (c == 'l') {

            *a = (struct term_action) {
               .type1 = a_enable_cursor,
               .arg = false,
            };
         }
         break;

      case 1049:
         if (c == 'h') {

            *a = (struct term_action) {
               .type1 = a_use_alt_buffer,
               .arg = true,
            };

         } else if (c == 'l') {

            *a = (struct term_action) {
               .type1 = a_use_alt_buffer,
               .arg = false,
            };

         }
         break;

      default:
         /* unknown private CSI extension: do nothing */
         break;
   }
}

typedef void (*csi_seq_handler)(u32 *params,
                                int pc,
                                u8 c,
                                u8 *color,
                                struct term_action *a,
                                struct twfilter_ctx_t *ctx);

static csi_seq_handler csi_handlers[256] =
{
   ['A'] = tty_filter_handle_csi_ABCD, /* CUU: Move the cursor up */
   ['B'] = tty_filter_handle_csi_ABCD, /* CUD: Move the cursor down */
   ['C'] = tty_filter_handle_csi_ABCD, /* CUF: Move the cursor right */
   ['D'] = tty_filter_handle_csi_ABCD, /* CUB: Move the cursor left */
   ['m'] = tty_filter_handle_csi_m,    /* SGR: Select Graphic Rendition */
   ['E'] = tty_csi_EF_handler,         /* CNL: Move N lines down; set col=0 */
   ['F'] = tty_csi_EF_handler,         /* CPL: Move N lines up; set col = 0 */
   ['G'] = tty_csi_G_handler,          /* CHA: Move to col N [abs, 1-based] */
   ['H'] = tty_csi_fH_handler,         /* CUP: Move to (N, M) [abs, 1-based] */
   ['f'] = tty_csi_fH_handler,         /* HVP: Move to (N, M) [abs, 1-based] */
   ['J'] = tty_csi_J_handler,          /* ED: Erase in display */
   ['K'] = tty_csi_K_handler,          /* EL: Erase in line */
   ['S'] = tty_csi_S_handler,          /* Non-buf scroll-up */
   ['T'] = tty_csi_T_handler,          /* Non-buf scroll-down */
   ['n'] = tty_csi_n_handler,          /* DSR: Device Status Report */
   ['s'] = tty_csi_s_handler,          /* SCP: Save Cursor Position */
   ['u'] = tty_csi_u_handler,          /* RCP: Restore Cursor Position */
   ['d'] = tty_csi_d_handler,          /* VPA: Move to row N (abs), same col */
   ['`'] = tty_csi_hpa_handler,        /* HPA: Move to col N (abs), same row */
};

static enum term_fret
tty_filter_end_csi_seq(u8 c,
                       u8 *color,
                       struct term_action *a,
                       struct twfilter_ctx_t *ctx)
{
   const char *endptr;
   u32 params[NPAR] = {0};
   bool csi_pvt_ext = false;
   int pc = 0;

   ctx->param_bytes[ctx->pbc] = 0;
   ctx->interm_bytes[ctx->ibc] = 0;

   if (ctx->pbc) {

      endptr = ctx->param_bytes - 1;

      if (ctx->param_bytes[0] == '?') {
         csi_pvt_ext = true;
         endptr++;
      }

      do {
         params[pc++] = (u32) tilck_strtol(endptr + 1, &endptr, NULL);
      } while (*endptr);
   }

   if (LIKELY(!csi_pvt_ext)) {

      if (LIKELY(csi_handlers[c] != NULL))
         csi_handlers[c](params, pc, c, color, a, ctx);

   } else {

      /* CSI ? <n> <c> */
      tty_csi_pvt_ext_handler(params, pc, c, color, a, ctx);
   }

   tty_reset_filter_ctx(ctx->t);
   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_state_esc2_csi(u8 *c, u8 *color, struct term_action *a, void *ctx_arg)
{
   struct twfilter_ctx_t *ctx = ctx_arg;
   int rc;

   if ((rc = tty_pre_filter(ctx, c)) >= 0)
      return (enum term_fret)rc;

   if (0x30 <= *c && *c <= 0x3F) {

      /* This is a parameter byte */

      if (ctx->pbc >= ARRAY_SIZE(ctx->param_bytes)) {

         /*
          * The param bytes exceed our limits, something gone wrong: just return
          * back to the default state ignoring this escape sequence.
          */

         tty_reset_filter_ctx(ctx->t);
         return TERM_FILTER_WRITE_BLANK;
      }

      ctx->param_bytes[ctx->pbc++] = (char) *c;
      return TERM_FILTER_WRITE_BLANK;
   }

   if (0x20 <= *c && *c <= 0x2F) {

      /* This is an "intermediate" byte */

      if (ctx->ibc >= ARRAY_SIZE(ctx->interm_bytes)) {

         /* As above, the param bytes exceed our limits */
         tty_reset_filter_ctx(ctx->t);
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
   tty_reset_filter_ctx(ctx->t);
   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_state_esc2_unknown(u8 *c, u8 *color, struct term_action *a, void *ctx_arg)
{
   struct twfilter_ctx_t *ctx = ctx_arg;
   int rc;

   if ((rc = tty_pre_filter(ctx, c)) >= 0)
      return (enum term_fret)rc;

   if (0x40 <= *c && *c <= 0x5f) {
      /* End of any possible (unknown) escape sequence */
      tty_set_state(ctx, &tty_state_default);
   }

   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_state_esc1(u8 *c, u8 *color, struct term_action *a, void *ctx_arg)
{
   struct twfilter_ctx_t *ctx = ctx_arg;
   int rc;

   if ((rc = tty_pre_filter(ctx, c)) >= 0)
      return (enum term_fret)rc;

   switch (*c) {

      case '[':
         tty_reset_filter_ctx(ctx->t);
         tty_set_state(ctx, &tty_state_esc2_csi);
         break;

      case 'c':
         {
            struct tty *t = ctx->t;
            *a = (struct term_action) { .type1 = a_reset };

            tty_inbuf_reset(t);
            t->kd_gfx_mode = KD_TEXT;
            t->curr_color = make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR);
            reset_console_data(t);
            tty_reset_termios(t);
            tty_update_default_state_tables(t);
            tty_reset_filter_ctx(t);
         }
         break;

      case '(':
         tty_set_state(ctx, &tty_state_esc2_par0);
         break;

      case ')':
         tty_set_state(ctx, &tty_state_esc2_par1);
         break;

      case 'D': /* linefeed */
         tty_set_state(ctx, &tty_state_default);
         return tty_def_state_lf(c, color, a, ctx_arg);

      case 'M': /* reverse linefeed */
         tty_set_state(ctx, &tty_state_default);
         return tty_def_state_ri(c, color, a, ctx_arg);

      default:
         tty_set_state(ctx, &tty_state_esc2_unknown);
         /*
          * We need to handle now this case because the sequence might be 1-char
          * long, like "^[_". Therefore, if the current character is in the
          * [0x40, 0x5f] range, we have to go back to the default state.
          * Otherwise, this is the identifier of a more complex sequence which
          * will end with the first character in the range [0x40, 0x5f].
          */

         return tty_state_esc2_unknown(c, color, a, ctx);
   }

   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_change_translation_table(struct twfilter_ctx_t *ctx, u8 *c, int c_set)
{
   struct console_data *const cd = ctx->cd;

   switch (*c) {

      case 'B':
         cd->c_sets_tables[c_set] = tty_default_trans_table;
         break;

      case '0':
         cd->c_sets_tables[c_set] = tty_gfx_trans_table;
         break;

      case 'U':
      case 'K':
      default:
         /* do nothing */
         break;
   }

   tty_set_state(ctx, &tty_state_default);
   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_state_esc2_par0(u8 *c, u8 *color, struct term_action *a, void *ctx_arg)
{
   struct twfilter_ctx_t *const ctx = ctx_arg;
   int rc;

   if ((rc = tty_pre_filter(ctx, c)) >= 0)
      return (enum term_fret)rc;

   return tty_change_translation_table(ctx_arg, c, 0);
}

static enum term_fret
tty_state_esc2_par1(u8 *c, u8 *color, struct term_action *a, void *ctx_arg)
{
   struct twfilter_ctx_t *const ctx = ctx_arg;
   int rc;

   if ((rc = tty_pre_filter(ctx, c)) >= 0)
      return (enum term_fret)rc;

   return tty_change_translation_table(ctx_arg, c, 1);
}

static void tty_set_state(struct twfilter_ctx_t *ctx, term_filter new_state)
{
   struct tty *const t = ctx->t;
   ctx->non_default_state = new_state != &tty_state_default;
   t->tintf->set_filter(t->tstate, new_state, ctx);
}

static int tty_pre_filter(struct twfilter_ctx_t *ctx, u8 *c)
{
   if (ctx->non_default_state) {

      switch (*c) {

         case '\033':
            /* ESC in the middle of any sequence, just starts a new one */
            tty_reset_filter_ctx(ctx->t);
            tty_set_state(ctx, &tty_state_esc1);
            return TERM_FILTER_WRITE_BLANK;

         default:
            /* fall-through */
            break;
      }
   }

   return -1;
}
