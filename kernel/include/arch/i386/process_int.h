
#pragma once

#include <process.h>
#include <list.h>
#include <arch/i386/arch_utils.h>

struct task_info {

   list_head list;

   int pid;
   int state;
   int exit_code;

   u64 jiffies_when_switch;

   /* tasklet members */

   int tasklet_id;
   void *kernel_stack;

   /* end tasklet members */

   regs state_regs;
   page_directory_t *pdir;
};

extern volatile u64 jiffies;
extern task_info *current_task;
extern int current_max_pid;
