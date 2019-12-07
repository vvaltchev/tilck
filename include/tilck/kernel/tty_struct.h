/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck_gen_headers/config_modules.h>
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/ringbuf.h>

#include <termios.h>      // system header
#include <linux/kd.h>     // system header

#include "../../../kernel/tty/term_int.h"   // HACK: just for term_filter

struct tty;
typedef bool (*tty_ctrl_sig_func)(struct tty *);

void tty_reset_filter_ctx(struct tty *t);
void tty_kb_buf_reset(struct tty *t);
void tty_reset_termios(struct tty *t);

struct twfilter_ctx_t {

   struct tty *t;

   char param_bytes[64];
   char interm_bytes[64];
   char tmpbuf[16];

   bool non_default_state;
   u8 pbc; /* param bytes count */
   u8 ibc; /* intermediate bytes count */
};


struct tty {

   struct term *term_inst;
   struct tilck_term_info term_i;

   int minor;
   char dev_filename[16];

   /* tty input */
   struct ringbuf input_ringbuf;
   struct kcond input_cond;
   int end_line_delim_count;
   bool mediumraw_mode;

   char *input_buf;
   tty_ctrl_sig_func *special_ctrl_handlers;

#if MOD_console
   u16 saved_cur_row;
   u16 saved_cur_col;
   u32 attrs;

   u8 user_color;       /* color before attrs */
   u8 c_set;            /* 0 = G0, 1 = G1     */
   const s16 *c_sets_tables[2];
   struct twfilter_ctx_t filter_ctx;
#endif

   /* tty ioctl */
   struct termios c_term;
   u32 kd_gfx_mode;

   /* tty input & output */
   u8 curr_color; /* actual color after applying attrs */
   u16 serial_port_fwd;

#if MOD_console
   term_filter default_state_funcs[256];
#endif
};
