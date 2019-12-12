/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/config.h>

#define MAX_NO_DEADLOCK_SET_ELEMS   144

void se_internal_run(void (*se_func)(void));
bool se_is_stop_requested(void);

void regular_self_test_end(void);
void simple_test_kthread(void *arg);
void selftest_kmalloc_perf_med(void);

/* Deadlock detection functions */
void debug_reset_no_deadlock_set(void);
void debug_add_task_to_no_deadlock_set(int tid);
void debug_remove_task_from_no_deadlock_set(int tid);
void debug_no_deadlock_set_report_progress(void);
void debug_check_for_deadlock(void);
void debug_check_for_any_progress(void);
