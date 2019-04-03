/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/config.h>

void regular_self_test_end(void);

#if KERNEL_SELFTESTS
   void kernel_run_selected_selftest(void);
#else
   static inline void kernel_run_selected_selftest(void) { }
#endif

void selftest_kmalloc_perf_med(void);
