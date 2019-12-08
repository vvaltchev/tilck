/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

struct tty;

void *alloc_console_data(void);
void free_console_data(void *data);
void init_console_data(struct tty *t);
void init_textmode_console(void);
