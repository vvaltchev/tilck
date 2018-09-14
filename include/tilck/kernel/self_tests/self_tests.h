/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

void sleeping_kthread(void *);
void simple_test_kthread(void *);

void selftest_fault_resumable_short();
void selftest_fault_resumable_perf_short();
void selftest_join_med();
void selftest_kmutex_med();
void selftest_kcond_med();
void selftest_tasklet_short();
void selftest_tasklet_perf_short();
void selftest_kmalloc_perf_med();

