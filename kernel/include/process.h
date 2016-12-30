
#pragma once

#include <common_defs.h>
#include <paging.h>
#include <irq.h>


struct task_info;
typedef struct task_info task_info;

void save_current_process_state(regs *);

NORETURN void schedule();
NORETURN void first_usermode_switch(page_directory_t *pdir,
                                    void *entry,
                                    void *stack_addr);
