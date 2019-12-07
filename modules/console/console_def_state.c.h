/* SPDX-License-Identifier: BSD-2-Clause */

static enum term_fret
tty_def_state_esc(u8 *c, u8 *color, struct term_action *a, void *ctx_arg)
{
   struct twfilter_ctx_t *const ctx = ctx_arg;
   tty_set_state(ctx, &tty_state_esc1);
   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_lf(u8 *c, u8 *color, struct term_action *a, void *ctx_arg)
{
   struct twfilter_ctx_t *const ctx = ctx_arg;

   if ((ctx->t->c_term.c_oflag & (OPOST | ONLCR)) == (OPOST | ONLCR)) {

      *a = (struct term_action) {
         .type3 = a_dwrite_no_filter,
         .len = 2,
         .col = *color,
         .ptr = (uptr)"\n\r",
      };

      return TERM_FILTER_WRITE_BLANK;
   }

   return TERM_FILTER_WRITE_C;
}

static enum term_fret
tty_def_state_ri(u8 *c, u8 *color, struct term_action *a, void *ctx_arg)
{
   struct twfilter_ctx_t *const ctx = ctx_arg;

   if (term_get_curr_row(ctx->t->term_inst) > 0) {

      *a = (struct term_action) {
         .type2 = a_move_ch_and_cur_rel,
         .arg1 = LO_BITS((u32)-1, 8, u32),
         .arg2 = 0,
      };

   } else {

      *a = (struct term_action) {
         .type2 = a_non_buf_scroll,
         .arg1 = 1,
         .arg2 = non_buf_scroll_down,
      };

   }

   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_keep(u8 *c, u8 *color, struct term_action *a, void *ctx_arg)
{
   return TERM_FILTER_WRITE_C;
}

static enum term_fret
tty_def_state_ignore(u8 *c, u8 *color, struct term_action *a, void *ctx_arg)
{
   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_shift_out(u8 *c, u8 *color, struct term_action *a, void *ctx_arg)
{
   struct twfilter_ctx_t *const ctx = ctx_arg;

   /* shift out: use alternate charset G1 */
   ctx->cd->c_set = 1;

   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_shift_in(u8 *c, u8 *color, struct term_action *a, void *ctx_arg)
{
   struct twfilter_ctx_t *const ctx = ctx_arg;

   /* shift in: return to the default charset G0 */
   ctx->cd->c_set = 0;

   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_verase(u8 *c, u8 *color, struct term_action *a, void *ctx_arg)
{
   *a = (struct term_action) {
      .type1 = a_del_generic,
      .arg = TERM_DEL_PREV_CHAR,
   };

   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_vwerase(u8 *c, u8 *color, struct term_action *a, void *ctx_arg)
{
   *a = (struct term_action) {
      .type1 = a_del_generic,
      .arg = TERM_DEL_PREV_WORD,
   };

   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_vkill(u8 *c, u8 *color, struct term_action *a, void *ctx_arg)
{
   /* TODO: add support for VKILL */
   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_csi(u8 *c, u8 *color, struct term_action *a, void *ctx_arg)
{
   struct twfilter_ctx_t *const ctx = ctx_arg;

   tty_reset_filter_ctx(ctx->t);
   tty_set_state(ctx, &tty_state_esc2_csi);
   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_backspace(u8 *c, u8 *color, struct term_action *a, void *ctx_arg)
{
   *a = (struct term_action) {
      .type2 = a_move_ch_and_cur_rel,
      .arg1 = LO_BITS((u32)  0, 8, u32),
      .arg2 = LO_BITS((u32) -1, 8, u32),
   };

   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_raw_lf(u8 *c, u8 *color, struct term_action *a, void *ctx_arg)
{
   /*
    * Typically, terminal emulators just treat \f and \v like a raw linefeed.
    * Raw linefeed means that the behavior is always just move the cursor to the
    * next line, without any carriage return.
    */

   *c = '\n';
   return TERM_FILTER_WRITE_C;
}

void tty_update_default_state_tables(struct tty *t)
{
   const struct termios *const c_term = &t->c_term;
   struct console_data *cd = t->console_data;
   bzero(cd->def_state_funcs, 256 * sizeof(term_filter));

   cd->def_state_funcs['\n'] = tty_def_state_lf;
   cd->def_state_funcs['\r'] = tty_def_state_keep;
   cd->def_state_funcs['\t'] = tty_def_state_keep;
   cd->def_state_funcs['\a'] = tty_def_state_ignore;   /* bell */
   cd->def_state_funcs['\f'] = tty_def_state_raw_lf;   /* form-feed */
   cd->def_state_funcs['\v'] = tty_def_state_raw_lf;   /* vertical tab */
   cd->def_state_funcs['\b'] = tty_def_state_backspace;
   cd->def_state_funcs['\033'] = tty_def_state_esc;
   cd->def_state_funcs['\016'] = tty_def_state_shift_out;
   cd->def_state_funcs['\017'] = tty_def_state_shift_in;
   cd->def_state_funcs[0x7f] = tty_def_state_ignore;
   cd->def_state_funcs[0x9b] = tty_def_state_csi;

   cd->def_state_funcs[c_term->c_cc[VERASE]] = tty_def_state_verase;
   cd->def_state_funcs[c_term->c_cc[VWERASE]] = tty_def_state_vwerase;
   cd->def_state_funcs[c_term->c_cc[VKILL]] = tty_def_state_vkill;
}

static enum term_fret
tty_def_print_untrasl_char(u8 *c,
                           u8 *color,
                           struct term_action *a,
                           void *ctx_arg)
{
   struct twfilter_ctx_t *const ctx = ctx_arg;
   int len = snprintk(ctx->tmpbuf, sizeof(ctx->tmpbuf), "{0x%x}", *c);

   *a = (struct term_action) {
      .type3 = a_dwrite_no_filter,
      .len = (u32)len,
      .col = *color,
      .ptr = (uptr)ctx->tmpbuf,
   };

   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_state_default(u8 *c, u8 *color, struct term_action *a, void *ctx_arg)
{
   struct twfilter_ctx_t *const ctx = ctx_arg;
   struct console_data *cd = ctx->cd;

   s16 tv = cd->c_sets_tables[cd->c_set][*c];
   int rc;

   if ((rc = tty_pre_filter(ctx, c)) >= 0)
      return (enum term_fret)rc;

   if (tv >= 0) {
      *c = (u8) tv;
      return TERM_FILTER_WRITE_C;
   }

   if (cd->def_state_funcs[*c])
      return cd->def_state_funcs[*c](c, color, a, ctx_arg);

   /* unknown character */
   *c = '?';
   return TERM_FILTER_WRITE_C;

   // DEBUG: uncomment for debugging untranslated characters
   // return tty_def_print_untrasl_char(c, color, a, ctx_arg);
}
