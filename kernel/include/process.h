
#pragma once

#include <common_defs.h>
#include <paging.h>
#include <irq.h>

// This is the biggest usermode addr + 1
#define OFFLIMIT_USERMODE_ADDR 0xC0000000UL

#define TASK_STATE_RUNNABLE 0
#define TASK_STATE_RUNNING 1
#define TASK_STATE_SLEEPING 2
#define TASK_STATE_ZOMBIE 3

struct task_info;
typedef struct task_info task_info;

void save_current_process_state(regs *);

NORETURN void schedule();
NORETURN void first_usermode_switch(page_directory_t *pdir,
                                    void *entry,
                                    void *stack_addr);

NORETURN void switch_to_process(task_info *pi);
void add_process(task_info *p);
void remove_process(task_info *p);

task_info *get_current_task();


typedef void (*tasklet_func_type)();

int create_kernel_tasklet(tasklet_func_type fun);

// Must be called at the end of each tasklet.
void exit_kernel_tasklet();
