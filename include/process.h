
#pragma once

#include <commonDefs.h>
#include <paging.h>
#include <irq.h>


struct process_info;
typedef struct process_info process_info;

void schedule(regs *r);
void first_usermode_switch(page_directory_t *pdir,
                           void *entry, void *stack_addr);
