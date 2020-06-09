/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/term.h>
#include <tilck/kernel/kb.h>
#include <tilck/kernel/tty_struct.h>
#include <tilck/kernel/tty.h>
#include <tilck/kernel/fs/vfs_base.h>
#include <tilck/kernel/fs/devfs.h>

void tty_input_init(struct tty *t);

enum kb_handler_action
tty_keypress_handler(struct kb_dev *, struct key_event ke);

void tty_update_ctrl_handlers(struct tty *t);
void tty_update_default_state_tables(struct tty *t);

ssize_t
tty_read_int(struct tty *t, struct devfs_handle *h, char *buf, size_t size);

ssize_t
tty_write_int(struct tty *t, struct devfs_handle *h, char *buf, size_t size);

int
tty_ioctl_int(struct tty *t, struct devfs_handle *h, ulong request, void *argp);

bool
tty_read_ready_int(struct tty *t, struct devfs_handle *h);

void init_ttyaux(void);
void tty_create_devfile_or_panic(const char *filename, u16 major, u16 minor);

extern const struct termios default_termios;
extern struct tty *ttys[128]; /* tty0 is not a real tty */
extern int tty_tasklet_runner;
extern struct tty *__curr_tty;
