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
void tty_reset_termios(struct tty *t);

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

   /* tty ioctl */
   struct termios c_term;
   u32 kd_gfx_mode;

   /* tty input & output */
   u8 curr_color; /* actual color after applying attrs */
   u16 serial_port_fwd;

   void *console_data;
};
