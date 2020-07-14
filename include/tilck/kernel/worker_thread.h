/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#define MAX_WORKER_THREADS 64

void init_worker_threads();
struct task *wth_get_task(int wth);
struct task *wth_get_runnable_thread(void);
int wth_create_thread(int priority, u16 queue_size);
u32 wth_get_queue_size(int wth);
NODISCARD bool wth_enqueue_job(int wth, void (*func)(void *), void *arg);
