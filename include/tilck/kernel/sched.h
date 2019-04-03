/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/bintree.h>


#define TIME_SLOT_TICKS (TIMER_HZ / 20)

typedef struct regs regs;
typedef struct task_info task_info;
typedef struct process_info process_info;

extern task_info *__current;
extern task_info *kernel_process;
extern process_info *kernel_process_pi;

extern list runnable_tasks_list;
extern list sleeping_tasks_list;
extern list zombie_tasks_list;

extern ATOMIC(u32) disable_preemption_count;

enum task_state {
   TASK_STATE_INVALID = 0,
   TASK_STATE_RUNNABLE = 1,
   TASK_STATE_RUNNING = 2,
   TASK_STATE_SLEEPING = 3,
   TASK_STATE_ZOMBIE = 4
};

void init_sched(void);
task_info *get_task(int tid);
void task_change_state(task_info *ti, enum task_state new_state);

static ALWAYS_INLINE void disable_preemption(void)
{
   atomic_fetch_add_explicit(&disable_preemption_count, 1U, mo_relaxed);
}

static ALWAYS_INLINE void enable_preemption(void)
{
   u32 oldval = atomic_fetch_sub_explicit(&disable_preemption_count,
                                          1U, mo_relaxed);

   ASSERT(oldval > 0);
   (void)oldval;
}

#ifdef DEBUG

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

static ALWAYS_INLINE task_info *get_curr_task(void)
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
void schedule(int curr_irq);
void schedule_outside_interrupt_context(void);

NORETURN void switch_to_task(task_info *ti, int curr_irq);
NORETURN void switch_to_idle_task(void);
NORETURN void switch_to_idle_task_outside_interrupt_context(void);

void save_current_task_state(regs *);
void account_ticks(void);
bool need_reschedule(void);
int create_new_pid(void);
void task_info_reset_kernel_stack(task_info *ti);
void add_task(task_info *ti);
void remove_task(task_info *ti);
void create_kernel_process(void);
void init_task_lists(task_info *ti);

// It is called when each kernel thread returns. May be called explicitly too.
void kthread_exit(void);

void kernel_sleep(u64 ticks);
void kthread_join(int tid);

void task_set_wakeup_timer(task_info *task, u32 ticks);
void task_update_wakeup_timer_if_any(task_info *ti, u32 new_ticks);
u32 task_cancel_wakeup_timer(task_info *ti);

typedef void (*kthread_func_ptr)();
NODISCARD int kthread_create(kthread_func_ptr fun, void *arg);
int iterate_over_tasks(bintree_visit_cb func, void *arg);
const char *debug_get_state_name(enum task_state state);

process_info *task_get_pi_opaque(task_info *ti);
void process_set_tty(process_info *pi, void *t);
bool in_currently_dying_task(void);
