
#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/elf_types.h>

uptr find_addr_of_symbol(const char *searched_sym);
const char *find_sym_at_addr(uptr vaddr, ptrdiff_t *offset, u32 *sym_size);
const char *find_sym_at_addr_safe(uptr vaddr, ptrdiff_t *offset, u32 *sym_size);
