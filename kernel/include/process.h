
#pragma once

#include <common_defs.h>
#include <paging.h>
#include <irq.h>

// This is the biggest usermode addr + 1
#define OFFLIMIT_USERMODE_ADDR 0xC0000000UL

#define TASK_STATE_RUNNABLE 0
#define TASK_STATE_RUNNING 1
#define TASK_STATE_SLEEPING 2
#define TASK_STATE_ZOMBIE 3

struct task_info;
typedef struct task_info task_info;

extern task_info *volatile current_task;

static ALWAYS_INLINE task_info *get_current_task()
{
   return current_task;
}

void save_current_task_state(regs *);

void account_ticks();
bool need_reschedule();

NORETURN void schedule();
NORETURN void schedule_outside_interrupt_context();

task_info *create_first_usermode_task(page_directory_t *pdir,
                                      void *entry,
                                      void *stack_addr);

bool is_kernel_thread(task_info *ti);

void set_current_task_in_kernel();
void set_current_task_in_user_mode();

task_info *get_task(int pid);

void task_info_reset_kernel_stack(task_info *ti);

void add_task(task_info *ti);
void remove_task(task_info *ti);

task_info *get_current_task();
void initialize_scheduler(void);

void task_change_state(task_info *ti, int new_state);

typedef void (*kthread_func_ptr)();

task_info *kthread_create(kthread_func_ptr fun, void *arg);

// It is called when each kernel thread returns. May be called explicitly too.
void kthread_exit();
