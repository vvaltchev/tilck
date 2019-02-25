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


#ifdef __clang__
   #pragma GCC diagnostic ignored "-Winitializer-overrides"
#else
   #pragma GCC diagnostic ignored "-Woverride-init"
#endif

const s16 tty_default_trans_table[256] =
{
   [0 ... 31] = -1,        /* not translated by default */

   [32] = 32,     [56] = 56,     [80] = 80,     [104] = 104,
   [33] = 33,     [57] = 57,     [81] = 81,     [105] = 105,
   [34] = 34,     [58] = 58,     [82] = 82,     [106] = 106,
   [35] = 35,     [59] = 59,     [83] = 83,     [107] = 107,
   [36] = 36,     [60] = 60,     [84] = 84,     [108] = 108,
   [37] = 37,     [61] = 61,     [85] = 85,     [109] = 109,
   [38] = 38,     [62] = 62,     [86] = 86,     [110] = 110,
   [39] = 39,     [63] = 63,     [87] = 87,     [111] = 111,
   [40] = 40,     [64] = 64,     [88] = 88,     [112] = 112,
   [41] = 41,     [65] = 65,     [89] = 89,     [113] = 113,
   [42] = 42,     [66] = 66,     [90] = 90,     [114] = 114,
   [43] = 43,     [67] = 67,     [91] = 91,     [115] = 115,
   [44] = 44,     [68] = 68,     [92] = 92,     [116] = 116,
   [45] = 45,     [69] = 69,     [93] = 93,     [117] = 117,
   [46] = 46,     [70] = 70,     [94] = 94,     [118] = 118,
   [47] = 47,     [71] = 71,     [95] = 95,     [119] = 119,
   [48] = 48,     [72] = 72,     [96] = 96,     [120] = 120,
   [49] = 49,     [73] = 73,     [97] = 97,     [121] = 121,
   [50] = 50,     [74] = 74,     [98] = 98,     [122] = 122,
   [51] = 51,     [75] = 75,     [99] = 99,     [123] = 123,
   [52] = 52,     [76] = 76,     [100] = 100,   [124] = 124,
   [53] = 53,     [77] = 77,     [101] = 101,   [125] = 125,
   [54] = 54,     [78] = 78,     [102] = 102,   [126] = 126,
   [55] = 55,     [79] = 79,     [103] = 103,

   [127 ... 255] = -1,     /* not translated */
};

const s16 tty_gfx_trans_table[256] =
{
   [0 ... 255] = -1,       /* not translated by default */

   [' '] = ' ',
   ['l'] = CHAR_ULCORNER,
   ['m'] = CHAR_LLCORNER,
   ['k'] = CHAR_URCORNER,
   ['j'] = CHAR_LRCORNER,
   ['t'] = CHAR_LTEE,
   ['u'] = CHAR_RTEE,
   ['v'] = CHAR_BTEE,
   ['w'] = CHAR_TTEE,
   ['q'] = CHAR_HLINE,
   ['x'] = CHAR_VLINE,
   ['n'] = CHAR_CROSS,
   ['`'] = CHAR_DIAMOND,
   ['a'] = CHAR_BLOCK_MID,
   ['f'] = CHAR_DEGREE,
   ['g'] = CHAR_PLMINUS,
   ['~'] = CHAR_BULLET,
   [','] = CHAR_LARROW,
   ['+'] = CHAR_RARROW,
   ['.'] = CHAR_DARROW,
   ['-'] = CHAR_UARROW,
   ['h'] = CHAR_BLOCK_LIGHT,
   ['0'] = CHAR_BLOCK_HEAVY
};

#pragma GCC diagnostic pop

void tty_reset_filter_ctx(twfilter_ctx_t *ctx)
{
   ctx->pbc = ctx->ibc = 0;
   ctx->state = TERM_WFILTER_STATE_DEFAULT;
}

static void
tty_filter_handle_csi_ABCD(u32 *params,
                           int pc,
                           u8 c,
                           u8 *color,
                           term_action *a,
                           twfilter_ctx_t *ctx)
{
   int d[4] = {0};
   d[c - 'A'] = (int) MAX(1u, params[0]);

   *a = (term_action) {
      .type2 = a_move_ch_and_cur_rel,
      .arg1 = LO_BITS((u32)(-d[0] + d[1]), 8, u32),
      .arg2 = LO_BITS((u32)( d[2] - d[3]), 8, u32)
   };
}

static void
tty_filter_handle_csi_m_param(u32 p, u8 *color, twfilter_ctx_t *ctx)
{
   tty *const t = ctx->t;

   u8 fg = get_color_fg(t->user_color);
   u8 bg = get_color_bg(t->user_color);

   switch(p) {

      case 0:
         /* Reset all attributes */
         fg = DEFAULT_FG_COLOR;
         bg = DEFAULT_BG_COLOR;
         t->attrs = 0;
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
         t->attrs |= TTY_ATTR_BOLD;
         goto set_color;

      case 7:
         t->attrs |= TTY_ATTR_REVERSE;
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
   t->user_color = make_color(fg, bg);

   if ((t->attrs & TTY_ATTR_BOLD) && fg <= 7)
      fg += 8;

   if (t->attrs & TTY_ATTR_REVERSE)
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
                        term_action *a,
                        twfilter_ctx_t *ctx)
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

static void
tty_csi_EF_handler(u32 *params,
                   int pc,
                   u8 c,
                   u8 *color,
                   term_action *a,
                   twfilter_ctx_t *ctx)
{
   tty *const t = ctx->t;
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
                  term_action *a,
                  twfilter_ctx_t *ctx)
{
   tty *const t = ctx->t;

   /* Move the cursor to the column 'n' (absolute, 1-based) */
   params[0] = MAX(1u, params[0]) - 1;

   *a = (term_action) {
      .type2 = a_move_ch_and_cur,
      .arg1 = term_get_curr_row(t->term_inst),
      .arg2 = MIN((u32)params[0], term_get_cols(t->term_inst) - 1u)
   };
}

static void
tty_csi_fH_handler(u32 *params,
                   int pc,
                   u8 c,
                   u8 *color,
                   term_action *a,
                   twfilter_ctx_t *ctx)
{
   tty *const t = ctx->t;

   /* Move the cursor to (n, m) (absolute, 1-based) */
   params[0] = MAX(1u, params[0]) - 1;
   params[1] = MAX(1u, params[1]) - 1;

   *a = (term_action) {
      .type2 = a_move_ch_and_cur,
      .arg1 = UNSAFE_MIN((u32)params[0], term_get_rows(t->term_inst)-1u),
      .arg2 = UNSAFE_MIN((u32)params[1], term_get_cols(t->term_inst)-1u)
   };
}

static void
tty_csi_J_handler(u32 *params,
                  int pc,
                  u8 c,
                  u8 *color,
                  term_action *a,
                  twfilter_ctx_t *ctx)
{
   *a = (term_action) { .type1 = a_erase_in_display, .arg = params[0] };
}

static void
tty_csi_K_handler(u32 *params,
                  int pc,
                  u8 c,
                  u8 *color,
                  term_action *a,
                  twfilter_ctx_t *ctx)
{
   *a = (term_action) { .type1 = a_erase_in_line, .arg = params[0] };
}

static void
tty_csi_S_handler(u32 *params,
                  int pc,
                  u8 c,
                  u8 *color,
                  term_action *a,
                  twfilter_ctx_t *ctx)
{
   *a = (term_action) {
      .type1 = a_non_buf_scroll_up,
      .arg = UNSAFE_MAX(1, params[0])
   };
}

static void
tty_csi_T_handler(u32 *params,
                  int pc,
                  u8 c,
                  u8 *color,
                  term_action *a,
                  twfilter_ctx_t *ctx)
{
   *a = (term_action) {
      .type1 = a_non_buf_scroll_down,
      .arg = UNSAFE_MAX(1, params[0])
   };
}

static void
tty_csi_n_handler(u32 *params,
                  int pc,
                  u8 c,
                  u8 *color,
                  term_action *a,
                  twfilter_ctx_t *ctx)
{
   tty *const t = ctx->t;

   if (params[0] == 6) {

      /* DSR (Device Status Report) */

      char dsr[16];
      snprintk(dsr, sizeof(dsr), "\033[%u;%uR",
               term_get_curr_row(t->term_inst) + 1,
               term_get_curr_col(t->term_inst) + 1);

      for (u8 *p = (u8 *)dsr; *p; p++)
         tty_keypress_handler_int(t, *p, *p, false);
   }
}

static void
tty_csi_s_handler(u32 *params,
                  int pc,
                  u8 c,
                  u8 *color,
                  term_action *a,
                  twfilter_ctx_t *ctx)
{
   tty *const t = ctx->t;

   /* SCP (Save Cursor Position) */
   ctx->t->saved_cur_row = term_get_curr_row(t->term_inst);
   ctx->t->saved_cur_col = term_get_curr_col(t->term_inst);
}

static void
tty_csi_u_handler(u32 *params,
                  int pc,
                  u8 c,
                  u8 *color,
                  term_action *a,
                  twfilter_ctx_t *ctx)
{
   tty *const t = ctx->t;

   /* RCP (Restore Cursor Position) */
   *a = (term_action) {
      .type2 = a_move_ch_and_cur,
      .arg1 = t->saved_cur_row,
      .arg2 = t->saved_cur_col
   };
}

static void
tty_csi_d_handler(u32 *params,
                  int pc,
                  u8 c,
                  u8 *color,
                  term_action *a,
                  twfilter_ctx_t *ctx)
{
   tty *const t = ctx->t;

   /* VPA: Move cursor to the indicated row, current column */
   params[0] = MAX(1u, params[0]) - 1;

   *a = (term_action) {
      .type2 = a_move_ch_and_cur,
      .arg1 = UNSAFE_MIN((u32)params[0], term_get_rows(t->term_inst)-1u),
      .arg2 = term_get_curr_col(t->term_inst)
   };
}

static void
tty_csi_hpa_handler(u32 *params,
                    int pc,
                    u8 c,
                    u8 *color,
                    term_action *a,
                    twfilter_ctx_t *ctx)
{
   tty *const t = ctx->t;

   /* HPA: Move cursor to the indicated column, current row */
   params[0] = MAX(1u, params[0]) - 1;

   *a = (term_action) {
      .type2 = a_move_ch_and_cur,
      .arg1 = term_get_curr_row(t->term_inst),
      .arg2 = UNSAFE_MIN((u32)params[0], term_get_cols(t->term_inst)-1u),
   };
}

typedef void (*csi_seq_handler)(u32 *params,
                                int pc,
                                u8 c,
                                u8 *color,
                                term_action *a,
                                twfilter_ctx_t *ctx);

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
   ['`'] = tty_csi_hpa_handler         /* HPA: Move to col N (abs), same row */
};

static enum term_fret
tty_filter_end_csi_seq(u8 c,
                       u8 *color,
                       term_action *a,
                       twfilter_ctx_t *ctx)
{
   const char *endptr;
   u32 params[NPAR] = {0};
   int pc = 0;

   ctx->param_bytes[ctx->pbc] = 0;
   ctx->interm_bytes[ctx->ibc] = 0;

   if (ctx->pbc) {

      endptr = ctx->param_bytes - 1;

      do {
         params[pc++] = (u32) tilck_strtol(endptr + 1, &endptr, NULL);
      } while (*endptr);
   }

   if (csi_handlers[c])
      csi_handlers[c](params, pc, c, color, a, ctx);

   tty_reset_filter_ctx(ctx);
   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_handle_csi_seq(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   twfilter_ctx_t *ctx = ctx_arg;

   if (0x30 <= *c && *c <= 0x3F) {

      /* This is a parameter byte */

      if (ctx->pbc >= ARRAY_SIZE(ctx->param_bytes)) {

         /*
          * The param bytes exceed our limits, something gone wrong: just return
          * back to the default state ignoring this escape sequence.
          */

         tty_reset_filter_ctx(ctx);
         return TERM_FILTER_WRITE_BLANK;
      }

      ctx->param_bytes[ctx->pbc++] = (char) *c;
      return TERM_FILTER_WRITE_BLANK;
   }

   if (0x20 <= *c && *c <= 0x2F) {

      /* This is an "intermediate" byte */

      if (ctx->ibc >= ARRAY_SIZE(ctx->interm_bytes)) {

         /* As above, the param bytes exceed our limits */
         tty_reset_filter_ctx(ctx);
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
   tty_reset_filter_ctx(ctx);
   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_handle_unknown_esc_seq(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   twfilter_ctx_t *ctx = ctx_arg;

   if (0x40 <= *c && *c <= 0x5f) {
      /* End of any possible (unknown) escape sequence */
      ctx->state = TERM_WFILTER_STATE_DEFAULT;
   }

   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_handle_state_esc1(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   twfilter_ctx_t *ctx = ctx_arg;

   switch (*c) {

      case '[':
         tty_reset_filter_ctx(ctx);
         ctx->state = TERM_WFILTER_STATE_ESC2_CSI;
         break;

      case 'c':
         {
            tty *t = ctx->t;
            *a = (term_action) { .type1 = a_reset };

            tty_kb_buf_reset(t);
            t->c_set = 0;
            t->c_sets_tables[0] = tty_default_trans_table;
            t->c_sets_tables[1] = tty_gfx_trans_table;
            t->c_term = default_termios;
            t->kd_mode = KD_TEXT;
            t->curr_color = make_color(DEFAULT_FG_COLOR, DEFAULT_BG_COLOR);
            t->user_color = t->curr_color;
            tty_update_default_state_tables(t);
            bzero(ctx, sizeof(*ctx));
            ctx->t = t;
         }
         break;

      case '(':
         ctx->state = TERM_WFILTER_STATE_ESC2_PAR0;
         break;

      case ')':
         ctx->state = TERM_WFILTER_STATE_ESC2_PAR1;
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
tty_change_translation_table(twfilter_ctx_t *ctx, u8 *c, int c_set)
{
   switch (*c) {

      case 'B':
         ctx->t->c_sets_tables[c_set] = tty_default_trans_table;
         break;

      case '0':
         ctx->t->c_sets_tables[c_set] = tty_gfx_trans_table;
         break;

      case 'U':
      case 'K':
      default:
         /* do nothing */
         break;
   }

   ctx->state = TERM_WFILTER_STATE_DEFAULT;
   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_handle_state_esc2_par0(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   return tty_change_translation_table(ctx_arg, c, 0);
}

static enum term_fret
tty_handle_state_esc2_par1(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   return tty_change_translation_table(ctx_arg, c, 1);
}

enum term_fret
tty_term_write_filter(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   static const term_filter table[] =
   {
      [TERM_WFILTER_STATE_DEFAULT] = &tty_handle_default_state,
      [TERM_WFILTER_STATE_ESC1] = &tty_handle_state_esc1,
      [TERM_WFILTER_STATE_ESC2_PAR0] = &tty_handle_state_esc2_par0,
      [TERM_WFILTER_STATE_ESC2_PAR1] = &tty_handle_state_esc2_par1,
      [TERM_WFILTER_STATE_ESC2_CSI] = &tty_handle_csi_seq,
      [TERM_WFILTER_STATE_ESC2_UNKNOWN] = &tty_handle_unknown_esc_seq
   };

   twfilter_ctx_t *ctx = ctx_arg;

   if (kopt_serial_mode == TERM_SERIAL_CONSOLE)
      return TERM_FILTER_WRITE_C;

   if (ctx->state != TERM_WFILTER_STATE_DEFAULT) {

      switch (*c) {

         case '\033':
            /* ESC in the middle of any sequence, just starts a new one */
            tty_reset_filter_ctx(ctx);
            ctx->state = TERM_WFILTER_STATE_ESC1;
            return TERM_FILTER_WRITE_BLANK;

         default:
            /* fall-through */
            break;
      }
   }

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
