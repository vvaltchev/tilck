/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/kernel/paging.h>
#include <tilck/kernel/hal.h>

extern const ulong init_st_begin;
extern const ulong init_st_end;

size_t stackwalk32(void **frames, size_t count,
                   void *ebp, pdir_t *pdir);

void dump_stacktrace(void *ebp, pdir_t *pdir);
void dump_regs(regs_t *r);

void validate_stack_pointer_int(const char *file, int line);
void debug_qemu_turn_off_machine(void);
void kmain_early_checks(void);
void init_extra_debug_features(void);
void set_sched_alive_thread_enabled(bool enabled);
void register_tilck_cmd(int cmd_n, void *func);
void *get_syscall_func_ptr(u32 n);
int get_syscall_num(void *func);

#if defined(DEBUG) && !defined(UNIT_TEST_ENVIRONMENT)

   #define DEBUG_VALIDATE_STACK_PTR() \
      validate_stack_pointer_int(__FILE__, __LINE__)

#else

   #define DEBUG_VALIDATE_STACK_PTR()

#endif

/*
 * Debug-only checks useful to verify that kernel_yield() + context_switch()
 * do NOT change the current ESP. Sure, at some point when we decide that
 * those function will never be touched again we could remove this code, but
 * until then, in a fast-growing and changing code base like the current one,
 * it makes sense to constantly check that there are *no* subtle bugs in the
 * probably most critical code.
 *
 * One of the places where the following two debug checks are used is inside
 * run_worker_thread()'s code: it is the perfect place for such checks, because
 * it really often yields and gets the control back.
 *
 * The DEBUG_VALIDATE_STACK_PTR() sure works as well, but it catches bugs only
 * when the stack pointer is completely out of the allocated stack area for the
 * current task. This following code allows instead, any kind of such problems
 * to be caught much earlier.
 */
#if !defined(NDEBUG) && !defined(RELEASE)

   #define DEBUG_SAVE_ESP()                     \
      ulong curr_esp;                           \
      ulong saved_esp = get_stack_ptr();

   #define DEBUG_CHECK_ESP()                                                 \
         curr_esp = get_stack_ptr();                                         \
         if (curr_esp != saved_esp)                                          \
            panic("ESP changed. Saved: %p, Curr: %p", saved_esp, curr_esp);

#else

   #define DEBUG_SAVE_ESP()
   #define DEBUG_CHECK_ESP()

#endif
