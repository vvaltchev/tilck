/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>
#include <tilck/kernel/hal_types.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/bintree.h>

#define TIME_SLOT_TICKS (TIMER_HZ / 20)

struct task;

extern struct task *__current;
extern struct task *kernel_process;
extern struct process *kernel_process_pi;

extern struct list runnable_tasks_list;
extern struct list sleeping_tasks_list;
extern struct list zombie_tasks_list;

extern ATOMIC(u32) disable_preemption_count;

enum task_state {
   TASK_STATE_INVALID   = 0,
   TASK_STATE_RUNNABLE  = 1,
   TASK_STATE_RUNNING   = 2,
   TASK_STATE_SLEEPING  = 3,
   TASK_STATE_ZOMBIE    = 4
};

void init_sched(void);
struct task *get_task(int tid);
void task_change_state(struct task *ti, enum task_state new_state);

static ALWAYS_INLINE void disable_preemption(void)
{
   atomic_fetch_add_explicit(&disable_preemption_count, 1U, mo_relaxed);
}

static ALWAYS_INLINE void enable_preemption(void)
{
   DEBUG_ONLY_UNSAFE(u32 oldval =)
   atomic_fetch_sub_explicit(&disable_preemption_count, 1U, mo_relaxed);

   ASSERT(oldval > 0);
}

/*
 * WARNING: this function is dangerous and should NEVER be used it for anything
 * other than special self-test code paths. See selftest_kmutex_ord_med().
 */
static ALWAYS_INLINE void force_enable_preemption(void)
{
   atomic_store_explicit(&disable_preemption_count, 0u, mo_relaxed);
}

#ifdef DEBUG

/*
 * This function is supposed to be used only in ASSERTs or in special debug-only
 * pieces of code. No "regular" code should use is_preemption_enabled() to
 * change its behavior.
 */
static ALWAYS_INLINE bool is_preemption_enabled(void)
{
   return disable_preemption_count == 0;
}

#endif

/*
 * Saves the current state and calls schedule().
 * That after, typically after some time, the scheduler will restore the thread
 * as if kernel_yield() returned and nothing else happened.
 */

void asm_kernel_yield(void);

/*
 * This wrapper is useful for adding ASSERTs and getting a backtrace containing
 * the caller's EIP in case of a failure.
 */
static inline void kernel_yield(void)
{
   ASSERT(is_preemption_enabled());
   asm_kernel_yield();
}

static ALWAYS_INLINE struct task *get_curr_task(void)
{
   /*
    * Access to `__current` DOES NOT need to be atomic (not even relaxed) even
    * on architectures (!= x86) where loading/storing a pointer-size integer
    * requires more than one instruction, for the following reasons:
    *
    *    - While ANY given task is running, `__current` is always set and valid.
    *      That is true even if the task is preempted after reading for example
    *      only half of its value and than its execution resumed back, because
    *      during the task switch the older value of `__current` will be
    *      restored.
    *
    *    - The `__current` variable is set only in three cases:
    *       - during initialization [create_kernel_process()]
    *       - in switch_to_task() [with interrupts disabled]
    *       - in kthread_exit() [with interrupts disabled]
    */
   return __current;
}

int get_curr_task_tid(void);
int get_curr_pid(void);
void schedule(int curr_int);
void schedule_outside_interrupt_context(void);

NORETURN void switch_to_task(struct task *ti, int curr_int);

void save_current_task_state(regs_t *);
void account_ticks(void);
bool need_reschedule(void);
int create_new_pid(void);
void task_info_reset_kernel_stack(struct task *ti);
void add_task(struct task *ti);
void remove_task(struct task *ti);
void create_kernel_process(void);
void init_task_lists(struct task *ti);

// It is called when each kernel thread returns. May be called explicitly too.
void kthread_exit(void);

void kernel_sleep(u64 ticks);
void kthread_join(int tid);
void kthread_join_all(const int *tids, size_t n);

void task_set_wakeup_timer(struct task *task, u32 ticks);
void task_update_wakeup_timer_if_any(struct task *ti, u32 new_ticks);
u32 task_cancel_wakeup_timer(struct task *ti);

typedef void (*kthread_func_ptr)();
NODISCARD int kthread_create(kthread_func_ptr fun, void *arg);
int iterate_over_tasks(bintree_visit_cb func, void *arg);

struct process *task_get_pi_opaque(struct task *ti);
void process_set_tty(struct process *pi, void *t);
bool in_currently_dying_task(void);
