/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/kernel/kb.h>

void dp_show_opts(void);
void do_show_tasks(void);
void dp_show_irq_stats(void);
void dp_show_sys_mmap(void);
void dp_show_kmalloc_heaps(void);

typedef struct {

   const char *label;
   void (*draw_func)(void);
   keypress_func on_keypress_func;

} dp_context;

extern int dp_rows;
extern int dp_cols;
