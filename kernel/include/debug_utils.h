
#pragma once

#include <paging.h>

size_t stackwalk32(void **frames, size_t count,
                   void *ebp, page_directory_t *pdir);

void dump_stacktrace();

uptr find_addr_of_symbol(const char *searched_sym);
const char *find_sym_at_addr(uptr vaddr, ptrdiff_t *offset);

