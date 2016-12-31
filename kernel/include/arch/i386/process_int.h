
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

   regs state_regs;
   page_directory_t *pdir;
};

extern volatile u64 jiffies;
extern volatile int current_interrupt_num;

extern task_info *current_process;
extern int current_max_pid;
