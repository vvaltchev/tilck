
#pragma once

#include <process.h>
#include <list.h>
#include <sync.h>
#include <arch/i386/arch_utils.h>

#define KTHREAD_STACK_SIZE (PAGE_SIZE)

struct task_info {

   list_node list;
   list_node runnable_list;
   list_node sleeping_list;

   int pid; /* global user/kernel thread identifier */
   task_state_enum state;
   int exit_code;

   u64 ticks;
   u64 total_ticks;
   u64 kernel_ticks;

   int running_in_kernel;

   int owning_process_pid; /* The pid of the process owning this thread. */
   void *kernel_stack;

   wait_obj wobj;

   regs state_regs;
   regs kernel_state_regs;
   page_directory_t *pdir;
};

extern volatile u64 jiffies;
extern int current_max_pid;
extern list_node tasks_list;
extern list_node runnable_tasks_list;
extern list_node sleeping_tasks_list;

static ALWAYS_INLINE u64 get_ticks()
{
   return jiffies;
}
