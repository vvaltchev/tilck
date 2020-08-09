/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#define WTH_PRIO_HIGHEST            0
#define WTH_PRIO_LOWEST           255

struct worker_thread;

void
init_worker_threads();

struct task *
wth_get_task(struct worker_thread *wth);

u32
wth_get_queue_size(struct worker_thread *wth);

int
wth_get_priority(struct worker_thread *wth);

const char *
wth_get_name(struct worker_thread *wth);

struct task *
wth_get_runnable_thread(void);

struct worker_thread *
wth_create_thread(const char *name, int priority, u16 queue_size);

struct worker_thread *
wth_find_worker(int lowest_prio);

NODISCARD bool
wth_enqueue_on(struct worker_thread *wth, void (*func)(void *), void *arg);

NODISCARD bool
wth_enqueue_anywhere(int lowest_prio, void (*func)(void *), void *arg);

void
wth_wait_for_completion(struct worker_thread *wth);
