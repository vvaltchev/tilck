/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/kernel/paging.h>
#include <tilck/kernel/hal.h>

extern const uptr init_st_begin;
extern const uptr init_st_end;

size_t stackwalk32(void **frames, size_t count,
                   void *ebp, pdir_t *pdir);

void dump_stacktrace(void *ebp, pdir_t *pdir);
void dump_regs(regs_t *r);

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

/* Deadlock detection */
void debug_reset_no_deadlock_set(void);
void debug_add_task_to_no_deadlock_set(int tid);
void debug_remove_task_from_no_deadlock_set(int tid);
void debug_check_for_deadlock(void);

/* Internal */
void register_tilck_cmd(int cmd_n, void *func);
