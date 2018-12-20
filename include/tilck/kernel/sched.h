/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/list.h>

typedef struct regs regs;
typedef struct task_info task_info;

extern task_info *__current;
extern task_info *kernel_process;

extern list_node runnable_tasks_list;
extern list_node sleeping_tasks_list;
extern list_node zombie_tasks_list;

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

extern ATOMIC(u32) disable_preemption_count;

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
void join_kernel_thread(int tid);

void task_set_wakeup_timer(task_info *task, u64 ticks);
void task_cancel_wakeup_timer(task_info *ti);
void task_update_wakeup_timer_if_any(task_info *ti, u64 new_ticks);

typedef void (*kthread_func_ptr)();
NODISCARD task_info *kthread_create(kthread_func_ptr fun, void *arg);
