/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/ringbuf.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/term.h>

#include <termios.h>      // system header
#include <linux/kd.h>     // system header

#include "term_int.h"

enum term_write_filter_state {

   TERM_WFILTER_STATE_DEFAULT,
   TERM_WFILTER_STATE_ESC1,
   TERM_WFILTER_STATE_ESC2_CSI,
   TERM_WFILTER_STATE_ESC2_PAR,
   TERM_WFILTER_STATE_ESC2_UNKNOWN

};

typedef struct {

   tty *t;

   enum term_write_filter_state state;
   char param_bytes[64];
   char interm_bytes[64];

   bool use_alt_charset;

   u8 pbc; /* param bytes count */
   u8 ibc; /* intermediate bytes count */

} term_write_filter_ctx_t;

void tty_input_init(tty *t);
void tty_kb_buf_reset(tty *t);
int tty_keypress_handler(u32 key, u8 c);

enum term_fret
tty_term_write_filter(u8 *c, u8 *color, term_action *a, void *ctx_arg);
void tty_update_special_ctrl_handlers(tty *t);
void tty_update_default_state_tables(tty *t);

ssize_t tty_read_int(tty *t, devfs_file_handle *h, char *buf, size_t size);
ssize_t tty_write_int(tty *t, devfs_file_handle *h, char *buf, size_t size);
int tty_ioctl_int(tty *t, devfs_file_handle *h, uptr request, void *argp);
int tty_fcntl_int(tty *t, devfs_file_handle *h, int cmd, uptr arg);
bool tty_read_ready_int(tty *t, devfs_file_handle *h);

void init_ttyaux(void);
void tty_create_devfile_or_panic(const char *filename, u16 major, u16 minor);


typedef bool (*tty_ctrl_sig_func)(tty *);

#define KB_INPUT_BS 1024

struct tty {

   term *term_inst;
   int minor;
   char dev_filename[16];

   /* tty input */
   ringbuf kb_input_ringbuf;
   kcond kb_input_cond;
   int kb_input_unread_cnt;
   int end_line_delim_count;

   /* tty output */
   u16 saved_cur_row;
   u16 saved_cur_col;
   term_write_filter_ctx_t filter_ctx;

   /* tty ioctl */
   struct termios c_term;
   u32 kd_mode;

   /* tty input & output */
   u8 curr_color;

   /* large fields */
   char kb_input_buf[KB_INPUT_BS];               /* tty input */
   tty_ctrl_sig_func special_ctrl_handlers[256]; /* tty input */
   term_filter default_state_funcs[256];         /* tty output */
};

extern const struct termios default_termios;
extern tty *ttys[MAX_TTYS + 1]; /* tty0 is not a real tty */
extern int tty_tasklet_runner;
