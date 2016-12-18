
#pragma once

#include <commonDefs.h>
#include <paging.h>
#include <irq.h>


struct task_info;
typedef struct task_info task_info;

void schedule();
void save_current_process_state(regs *);
int fork_current_process();
void exit_current_process(int exit_code);

void first_usermode_switch(page_directory_t *pdir,
                           void *entry, void *stack_addr);
