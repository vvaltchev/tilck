
#pragma once

#include <process.h>
#include <list.h>
#include <arch/i386/arch_utils.h>

#define KERNEL_TASKLET_STACK_SIZE PAGE_SIZE

struct task_info {

   list_head list;

   int pid;
   int state;
   int exit_code;

   u64 jiffies_when_switch;

   /* kernel thread members */

   /*
    * By adding this int above the bool is_kthread, we crash.
    * If the bool in turned into an 'int' instead, everything is fine.
    * This requires investigation.
    */
   //int task_process_pid; /* The pid of the process owning this thread. */

   bool is_kthread;
   void *kernel_stack;

   /* end tasklet members */

   regs state_regs;
   page_directory_t *pdir;
};

extern volatile u64 jiffies;
extern task_info *volatile current_task;
extern int current_max_pid;
