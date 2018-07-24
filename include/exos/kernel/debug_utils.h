
#pragma once

#include <exos/kernel/paging.h>
#include <exos/kernel/hal.h>

size_t stackwalk32(void **frames, size_t count,
                   void *ebp, page_directory_t *pdir);

void dump_stacktrace(void);
void dump_regs(regs *r);

void validate_stack_pointer_int(const char *file, int line);

#ifdef DEBUG
#  define DEBUG_VALIDATE_STACK_PTR() validate_stack_pointer_int(__FILE__, \
                                                                __LINE__)
#else
#  define DEBUG_VALIDATE_STACK_PTR()
#endif

void debug_qemu_turn_off_machine(void);
void debug_show_build_opts(void);
