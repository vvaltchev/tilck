
#pragma once

#include <common/basic_defs.h>
#include <exos/list.h>
#include <exos/paging.h>
#include <exos/irq.h>
#include <exos/hal.h>
#include <exos/sync.h>
#include <exos/fs/exvfs.h>

// This is the biggest usermode addr + 1
#define OFFLIMIT_USERMODE_ADDR 0xC0000000UL

#define KTHREAD_STACK_SIZE (PAGE_SIZE)
#define MAX_PID 65535

typedef enum {
   TASK_STATE_RUNNABLE = 0,
   TASK_STATE_RUNNING = 1,
   TASK_STATE_SLEEPING = 2,
   TASK_STATE_ZOMBIE = 3
} task_state_enum;

struct task_info {

   list_node list;
   list_node runnable_list;
   list_node sleeping_list;

   int tid; /* global user/kernel task (thread) identifier */
   int owning_process_pid; /* The ID of the owner process (like Linux's tgid) */
   int parent_tid;

   task_state_enum state;
   u8 exit_status;
   bool running_in_kernel;

   u64 ticks;
   u64 total_ticks;
   u64 kernel_ticks;

   void *kernel_stack;

   wait_obj wobj;

   regs state_regs;
   regs *kernel_state_regs;
   page_directory_t *pdir;

   char cwd[256]; /* current working directory */

   /* Handles */
   fs_handle handles[16]; /* for the moment, just a fixed-size small array */
};

typedef struct task_info task_info;

extern volatile u64 jiffies;
extern task_info *current;

extern list_node tasks_list;
extern list_node runnable_tasks_list;
extern list_node sleeping_tasks_list;

static ALWAYS_INLINE bool running_in_kernel(task_info *t)
{
   return t->running_in_kernel;
}

static ALWAYS_INLINE u64 get_ticks(void)
{
   return jiffies;
}

static ALWAYS_INLINE task_info *get_current_task(void)
{
   return current;
}

static ALWAYS_INLINE bool is_kernel_thread(task_info *ti)
{
   return ti->owning_process_pid == 0;
}

void save_current_task_state(regs *);
void account_ticks(void);
bool need_reschedule(void);

NORETURN void switch_to_task(task_info *ti);

void schedule(void);
void schedule_outside_interrupt_context(void);
NORETURN void switch_to_idle_task(void);
NORETURN void switch_to_idle_task_outside_interrupt_context(void);

task_info *create_usermode_task(page_directory_t *pdir,
                                void *entry,
                                void *stack_addr,
                                task_info *task_to_use,
                                char *const *argv,
                                char *const *env);

void set_current_task_in_kernel(void);
void set_current_task_in_user_mode(void);

int create_new_pid(void);

task_info *get_task(int tid);

void task_info_reset_kernel_stack(task_info *ti);

void add_task(task_info *ti);
void remove_task(task_info *ti);
void initialize_scheduler(void);

void task_change_state(task_info *ti, task_state_enum new_state);

typedef void (*kthread_func_ptr)();

task_info *kthread_create(kthread_func_ptr fun, void *arg);

// It is called when each kernel thread returns. May be called explicitly too.
void kthread_exit(void);

/*
 * Saves the current state and calls schedule().
 * That after, typically after some time, the scheduler will restore the thread
 * as if kernel_yield() returned and nothing else happened.
 */

void kernel_yield(void);

void kernel_sleep(u64 ticks);

int set_task_to_wake_after(task_info *task, u64 ticks);
void cancel_timer(int timer_num);


// TODO: consider moving these functions and the sched ones in sched.h

extern volatile u32 disable_preemption_count;

static ALWAYS_INLINE void disable_preemption(void) {
   disable_preemption_count++;
}

static ALWAYS_INLINE void enable_preemption(void) {
   ASSERT(disable_preemption_count > 0);
   disable_preemption_count--;
}

static ALWAYS_INLINE bool is_preemption_enabled(void) {
   return disable_preemption_count == 0;
}
