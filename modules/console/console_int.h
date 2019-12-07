/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include "term_int.h"

#include <termios.h>      // system header

#define NPAR 16                   /* maximum number of CSI parameters */
#define TTY_ATTR_BOLD             (1 << 0)
#define TTY_ATTR_REVERSE          (1 << 1)

struct console_data;

struct twfilter_ctx_t {

   struct tty *t;
   struct console_data *cd;

   char param_bytes[64];
   char interm_bytes[64];
   char tmpbuf[16];

   bool non_default_state;
   u8 pbc; /* param bytes count */
   u8 ibc; /* intermediate bytes count */
};

struct console_data {

   u16 saved_cur_row;
   u16 saved_cur_col;
   u32 attrs;

   u8 user_color;       /* color before attrs */
   u8 c_set;            /* 0 = G0, 1 = G1     */
   const s16 *c_sets_tables[2];
   struct twfilter_ctx_t filter_ctx;

   term_filter *def_state_funcs;
};

static int tty_pre_filter(struct twfilter_ctx_t *ctx, u8 *c);
static void tty_set_state(struct twfilter_ctx_t *ctx, term_filter new_state);
static enum term_fret tty_state_default(u8*, u8*, struct term_action*, void*);
static enum term_fret tty_state_esc1(u8*, u8*, struct term_action*, void*);
static enum term_fret tty_state_esc2_par0(u8*, u8*, struct term_action*, void*);
static enum term_fret tty_state_esc2_par1(u8*, u8*, struct term_action*, void*);
static enum term_fret tty_state_esc2_csi(u8*, u8*, struct term_action*, void*);
static enum term_fret tty_state_esc2_unknown(u8*,u8*,struct term_action*,void*);


extern const u8 fg_csi_to_vga[256];
extern const s16 tty_default_trans_table[256];
extern const s16 tty_gfx_trans_table[256];
