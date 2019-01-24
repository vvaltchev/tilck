/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

struct tty;
typedef struct tty tty;

extern tty *__curr_tty;

static ALWAYS_INLINE tty *get_curr_tty(void)
{
   return __curr_tty;
}


void init_tty(void);
void tty_setup_for_panic(tty *t);
int tty_keypress_handler_int(tty *t, u32 key, u8, bool check_mods);
