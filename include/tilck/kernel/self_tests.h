/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/config.h>

void se_internal_run(void (*se_func)(void));
bool se_is_stop_requested(void);

void regular_self_test_end(void);
void simple_test_kthread(void *arg);
void selftest_kmalloc_perf_med(void);
