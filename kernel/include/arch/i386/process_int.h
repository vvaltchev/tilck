
#pragma once

#include <process.h>
#include <list.h>
#include <arch/i386/arch_utils.h>

#define TASK_STATE_RUNNABLE 0
#define TASK_STATE_RUNNING 1
#define TASK_STATE_SLEEPING 2
#define TASK_STATE_ZOMBIE 3

struct task_info {

   list_head list;

   int pid;
   int state;
   int exit_code;

   u64 jiffies_when_switch;

   regs state_regs;
   page_directory_t *pdir;
};
