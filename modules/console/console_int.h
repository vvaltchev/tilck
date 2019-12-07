/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

static int tty_pre_filter(struct twfilter_ctx_t *ctx, u8 *c);
static void tty_set_state(struct twfilter_ctx_t *ctx, term_filter new_state);
static enum term_fret tty_state_default(u8*, u8*, struct term_action*, void*);
static enum term_fret tty_state_esc1(u8*, u8*, struct term_action*, void*);
static enum term_fret tty_state_esc2_par0(u8*, u8*, struct term_action*, void*);
static enum term_fret tty_state_esc2_par1(u8*, u8*, struct term_action*, void*);
static enum term_fret tty_state_esc2_csi(u8*, u8*, struct term_action*, void*);
static enum term_fret tty_state_esc2_unknown(u8*,u8*,struct term_action*,void*);

#define NPAR 16 /* maximum number of CSI parameters */
#define TTY_ATTR_BOLD             (1 << 0)
#define TTY_ATTR_REVERSE          (1 << 1)
