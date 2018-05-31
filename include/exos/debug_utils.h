
#pragma once

#include <exos/paging.h>
#include <exos/hal.h>

size_t stackwalk32(void **frames, size_t count,
                   void *ebp, page_directory_t *pdir);

void dump_stacktrace(void);
void dump_regs(regs *r);

uptr find_addr_of_symbol(const char *searched_sym);
const char *find_sym_at_addr(uptr vaddr, ptrdiff_t *offset, u32 *sym_size);

void validate_stack_pointer_int(const char *file, int line);

#ifdef DEBUG
#  define DEBUG_VALIDATE_STACK_PTR() validate_stack_pointer_int(__FILE__, \
                                                                __LINE__)
#else
#  define DEBUG_VALIDATE_STACK_PTR()
#endif

// Turn off the machine using a debug qemu-only mechnism
void debug_qemu_turn_off_machine();

