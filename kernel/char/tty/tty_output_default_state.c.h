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
tty_handle_default_state(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   term_write_filter_ctx_t *const ctx = ctx_arg;
   tty *const t = ctx->t;
   struct termios *const c_term = &t->c_term;

   if (ctx->use_alt_charset && alt_charset[*c] != -1) {
      *c = (u8) alt_charset[*c];
      return TERM_FILTER_WRITE_C;
   }

   switch (*c) {

      case '\033':
         ctx->state = TERM_WFILTER_STATE_ESC1;
         return TERM_FILTER_WRITE_BLANK;

      case '\n':

         if (c_term->c_oflag & (OPOST | ONLCR)) {

            *a = (term_action) {
               .type3 = a_dwrite_no_filter,
               .len = 2,
               .col = *color,
               .ptr = (uptr)"\n\r"
            };

            return TERM_FILTER_WRITE_BLANK;
         }

         return TERM_FILTER_WRITE_C;

      case '\a':
      case '\f':
      case '\v':
         /* Ignore some characters */
         return TERM_FILTER_WRITE_BLANK;

      case '\016': /* shift out: use alternate charset */
         ctx->use_alt_charset = true;
         return TERM_FILTER_WRITE_BLANK;

      case '\017': /* shift in: return to the regular charset */
         ctx->use_alt_charset = false;
         return TERM_FILTER_WRITE_BLANK;
   }

   if (*c == c_term->c_cc[VERASE]) {

      *a = (term_action) {
         .type1 = a_del,
         .arg = TERM_DEL_PREV_CHAR
      };

      return TERM_FILTER_WRITE_BLANK;

   } else if (*c == c_term->c_cc[VWERASE]) {

      *a = (term_action) {
         .type1 = a_del,
         .arg = TERM_DEL_PREV_WORD
      };

      return TERM_FILTER_WRITE_BLANK;

   } else if (*c == c_term->c_cc[VKILL]) {

      /* TODO: add support for KILL in tty */
      return TERM_FILTER_WRITE_BLANK;
   }

   return TERM_FILTER_WRITE_C;
}
