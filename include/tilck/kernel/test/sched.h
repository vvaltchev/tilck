/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>
#include <tilck/kernel/sched.h>

STATIC struct task *
sched_do_select_runnable_task(enum task_state curr_state, bool resched);

STATIC u64 sched_compute_avg_vruntime(void);

#ifdef UNIT_TEST_ENVIRONMENT
   /*
    * sched.c file-scope statics surfaced for unit tests via STATIC
    * (kept `static` in real kernel builds, external linkage under
    * UNIT_TEST_ENVIRONMENT). Read-only from the tests in normal use;
    * the wake-handoff underflow test additionally writes to
    * min_vruntime to set up the < BONUS scenario.
    */
   extern atomic_u64_t min_vruntime;
   extern atomic_u64_t sum_vruntime_in_tree;
#endif

/*
 * sched.c-local algorithm constants surfaced for tests. Kept in
 * sync with sched.c by convention — a value drift breaks the tests
 * deterministically, which is the desired signal.
 */
#define SCHED_TEST_VRUNTIME_SCALE         16
#define SCHED_TEST_WAKEUP_VRUNTIME_BONUS  (10 * SCHED_TEST_VRUNTIME_SCALE)
