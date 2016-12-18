
#pragma once

#include <process.h>
#include <arch/i386/arch_utils.h>

#define TASK_STATE_RUNNABLE 0
#define TASK_STATE_RUNNING 1
#define TASK_STATE_SLEEPING 2
#define TASK_STATE_ZOMBIE 3

struct process_info {

   process_info *next;
   process_info *prev;

   int pid;
   int state;
   int exit_code;

   regs state_regs;
   page_directory_t *pdir;
};
