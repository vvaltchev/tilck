
#pragma once

#include <paging.h>
#include <hal.h>

size_t stackwalk32(void **frames, size_t count,
                   void *ebp, page_directory_t *pdir);

void dump_stacktrace(void);
void dump_regs(regs *r);

uptr find_addr_of_symbol(const char *searched_sym);
const char *find_sym_at_addr(uptr vaddr, ptrdiff_t *offset);

