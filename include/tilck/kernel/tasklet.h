/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#define MAX_TASKLET_THREADS 64

void init_tasklets();
struct task *get_tasklet_runner(u32 tn);
int create_tasklet_thread(int priority, u16 limit);
bool any_tasklets_to_run(u32 tn);
u32 get_tasklet_runner_limit(u32 tn);
void tasklet_runner(void *arg);
struct task *get_hi_prio_ready_tasklet_runner(void);

NODISCARD bool
enqueue_tasklet(int tn, void (*func)(void *), void *arg);
