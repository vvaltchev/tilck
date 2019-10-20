/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/kernel/paging.h>
#include <tilck/kernel/hal.h>

size_t stackwalk32(void **frames, size_t count,
                   void *ebp, pdir_t *pdir);

void dump_stacktrace(void);
void dump_regs(struct regs *r);

void validate_stack_pointer_int(const char *file, int line);

#if defined(DEBUG) && !defined(UNIT_TEST_ENVIRONMENT)
#  define DEBUG_VALIDATE_STACK_PTR() validate_stack_pointer_int(__FILE__, \
                                                                __LINE__)
#else
#  define DEBUG_VALIDATE_STACK_PTR()
#endif

void debug_qemu_turn_off_machine(void);
void init_extra_debug_features();
void set_sched_alive_thread_enabled(bool enabled);
