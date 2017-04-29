
#pragma once

#include <process.h>
#include <list.h>
#include <arch/i386/arch_utils.h>

#define KTHREAD_STACK_SIZE PAGE_SIZE

struct task_info {

   list_head list;

   int pid; /* global user/kernel thread identifier */
   int state;
   int exit_code;

   u64 jiffies_when_switch;

   u64 ticks;

   int owning_process_pid; /* The pid of the process owning this thread. */
   void *kernel_stack;

   regs state_regs;
   page_directory_t *pdir;
};

extern volatile u64 jiffies;
extern task_info *volatile current_task;
extern int current_max_pid;
