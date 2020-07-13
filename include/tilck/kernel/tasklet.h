/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#define MAX_WORKER_THREADS 64

void init_worker_threads();
struct task *get_worker_thread(int wth);
int create_worker_thread(int priority, u16 limit);
u32 get_worker_queue_size(int wth);
struct task *get_runnable_worker_thread(void);

NODISCARD bool
enqueue_job(int wth, void (*func)(void *), void *arg);
