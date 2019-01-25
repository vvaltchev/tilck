/* SPDX-License-Identifier: BSD-2-Clause */

#pragma GCC diagnostic push

#ifdef __clang__
   #pragma GCC diagnostic ignored "-Winitializer-overrides"
#else
   #pragma GCC diagnostic ignored "-Woverride-init"
#endif

static const s16 alt_charset[256] =
{
   [0 ... 255] = -1,

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

static enum term_fret
tty_def_state_esc(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   term_write_filter_ctx_t *const ctx = ctx_arg;

   ctx->state = TERM_WFILTER_STATE_ESC1;
   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_nl(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   term_write_filter_ctx_t *const ctx = ctx_arg;

   if (ctx->t->c_term.c_oflag & (OPOST | ONLCR)) {

      *a = (term_action) {
         .type3 = a_dwrite_no_filter,
         .len = 2,
         .col = *color,
         .ptr = (uptr)"\n\r"
      };

      return TERM_FILTER_WRITE_BLANK;
   }

   return TERM_FILTER_WRITE_C;
}

static enum term_fret
tty_def_state_ignore(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_shift_out(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   term_write_filter_ctx_t *const ctx = ctx_arg;

   /* shift out: use alternate charset */
   ctx->use_alt_charset = true;
   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_shift_in(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   term_write_filter_ctx_t *const ctx = ctx_arg;

   /* shift in: return to the regular charset */
   ctx->use_alt_charset = false;
   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_verase(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   *a = (term_action) {
      .type1 = a_del,
      .arg = TERM_DEL_PREV_CHAR
   };

   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_vwerase(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   *a = (term_action) {
      .type1 = a_del,
      .arg = TERM_DEL_PREV_WORD
   };

   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_vkill(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   /* TODO: add support for VKILL */
   return TERM_FILTER_WRITE_BLANK;
}

void tty_update_default_state_tables(tty *t)
{
   const struct termios *const c_term = &t->c_term;
   bzero(t->default_state_funcs, sizeof(t->default_state_funcs));

   t->default_state_funcs['\033'] = tty_def_state_esc;
   t->default_state_funcs['\n'] = tty_def_state_nl;
   t->default_state_funcs['\a'] = tty_def_state_ignore;
   t->default_state_funcs['\f'] = tty_def_state_ignore;
   t->default_state_funcs['\v'] = tty_def_state_ignore;
   t->default_state_funcs['\016'] = tty_def_state_shift_out;
   t->default_state_funcs['\017'] = tty_def_state_shift_in;

   t->default_state_funcs[c_term->c_cc[VERASE]] = tty_def_state_verase;
   t->default_state_funcs[c_term->c_cc[VWERASE]] = tty_def_state_vwerase;
   t->default_state_funcs[c_term->c_cc[VKILL]] = tty_def_state_vkill;
}

static enum term_fret
tty_handle_default_state(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   term_write_filter_ctx_t *const ctx = ctx_arg;
   tty *const t = ctx->t;

   if (ctx->use_alt_charset && alt_charset[*c] != -1) {
      *c = (u8) alt_charset[*c];
      return TERM_FILTER_WRITE_C;
   }

   if (t->default_state_funcs[*c])
      return t->default_state_funcs[*c](c, color, a, ctx_arg);

   return TERM_FILTER_WRITE_C;
}
