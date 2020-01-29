/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck_gen_headers/config_modules.h>
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/ringbuf.h>

#include <termios.h>      // system header
#include <linux/kd.h>     // system header

struct tty;
typedef bool (*tty_ctrl_sig_func)(struct tty *);

void tty_reset_filter_ctx(struct tty *t);
void tty_inbuf_reset(struct tty *t);

struct tty {

   term_t tstate;
   const struct term_interface *tintf;
   struct term_params tparams;
   void *console_data;

   int minor;
   int fg_pgid;
   char dev_filename[16];

   struct ringbuf input_ringbuf;
   struct kcond input_cond;
   int end_line_delim_count;

   bool mediumraw_mode;
   u8 curr_color;
   u16 serial_port_fwd;

   char *input_buf;
   u32 kd_gfx_mode;
   tty_ctrl_sig_func *ctrl_handlers;
   struct termios c_term;
};
