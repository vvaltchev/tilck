/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/kb.h>

struct tty;
extern struct tty *__curr_tty;

static ALWAYS_INLINE struct tty *get_curr_tty(void)
{
   return __curr_tty;
}

void init_tty(void);
void tty_setup_for_panic(struct tty *t);
int tty_keypress_handler_int(struct tty *t, struct key_event, bool check_mods);
int tty_get_num(struct tty *t);
void tty_restore_kd_text_mode(struct tty *t);

static inline int get_curr_tty_num(void)
{
   return tty_get_num(get_curr_tty());
}

/* Used only by the debug panel */
int set_curr_tty(struct tty *t);
struct tty *create_tty_nodev(void);
