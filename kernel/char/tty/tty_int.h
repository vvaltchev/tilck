/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/ringbuf.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/fs/devfs.h>

#include <termios.h>      // system header
#include <linux/kd.h>     // system header

#include "term_int.h"

#define NPAR 16 /* maximum number of CSI parameters */
#define TTY_ATTR_BOLD             (1 << 0)
#define TTY_ATTR_REVERSE          (1 << 1)

struct twfilter_ctx_t {

   tty *t;

   char param_bytes[64];
   char interm_bytes[64];
   char tmpbuf[16];

   bool non_default_state;
   u8 pbc; /* param bytes count */
   u8 ibc; /* intermediate bytes count */
};

void tty_input_init(tty *t);
void tty_kb_buf_reset(tty *t);
int tty_keypress_handler(u32 key, u8 c);
void tty_reset_filter_ctx(tty *t);

enum term_fret
serial_tty_write_filter(u8 *c, u8 *color, struct term_action *a, void *ctx_arg);

void tty_update_special_ctrl_handlers(tty *t);
void tty_update_default_state_tables(tty *t);

ssize_t tty_read_int(tty *t, struct devfs_handle *h, char *buf, size_t size);
ssize_t tty_write_int(tty *t, struct devfs_handle *h, char *buf, size_t size);
int tty_ioctl_int(tty *t, struct devfs_handle *h, uptr request, void *argp);
int tty_fcntl_int(tty *t, struct devfs_handle *h, int cmd, int arg);
bool tty_read_ready_int(tty *t, struct devfs_handle *h);

void init_ttyaux(void);
void tty_create_devfile_or_panic(const char *filename, u16 major, u16 minor);

typedef bool (*tty_ctrl_sig_func)(tty *);

struct tty {

   term *term_inst;
   int minor;
   char dev_filename[16];

   /* tty input */
   struct ringbuf input_ringbuf;
   struct kcond input_cond;
   int end_line_delim_count;

   /* tty output */
   u16 saved_cur_row;
   u16 saved_cur_col;
   u32 attrs;

   u8 c_set; // 0 = G0, 1 = G1.
   const s16 *c_sets_tables[2];
   struct twfilter_ctx_t filter_ctx;

   /* tty ioctl */
   struct termios c_term;
   u32 kd_mode;

   /* tty input & output */
   u8 curr_color; /* actual color after applying attrs */
   u8 user_color; /* color before attrs */
   u16 serial_port_fwd;

   /* large fields */
   char input_buf[TTY_INPUT_BS];                  /* tty input */
   tty_ctrl_sig_func special_ctrl_handlers[256];  /* tty input */
   term_filter default_state_funcs[256];          /* tty output */
};

extern const struct termios default_termios;
extern tty *ttys[128]; /* tty0 is not a real tty */
extern int tty_tasklet_runner;
extern const s16 tty_default_trans_table[256];
extern const s16 tty_gfx_trans_table[256];
