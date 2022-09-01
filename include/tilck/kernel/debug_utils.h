/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/kernel/paging.h>
#include <tilck/kernel/hal.h>

extern const ulong init_st_begin;
extern const ulong init_st_end;

void dump_stacktrace(void *ebp, pdir_t *pdir);
void dump_regs(regs_t *r);

int debug_qemu_turn_off_machine(void);
void kmain_early_checks(void);
void init_extra_debug_features(void);
int set_sched_alive_thread_enabled(bool enabled);
void register_tilck_cmd(int cmd_n, void *func);
void *get_syscall_func_ptr(u32 n);
int get_syscall_num(void *func);

/*
 * Debug-only checks useful to verify that kernel_yield() + context_switch()
 * do NOT change the current ESP. Sure, at some point when we decide that
 * those function will never be touched again we could remove this code, but
 * until then, in a fast-growing and changing code base like the current one,
 * it makes sense to constantly check that there are *no* subtle bugs in the
 * probably most critical code.
 *
 * One of the places where the following two debug checks are used is inside
 * wth_run()'s code: it is the perfect place for such checks, because
 * it really often yields and gets the control back.
 *
 * The KERNEL_STACK_ISOLATION sure works as well, but it catches bugs only
 * when the stack pointer is completely out of the allocated stack area for the
 * current task. With following macros allows instead, any kind of such problems
 * will be caught much earlier.
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
