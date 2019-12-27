/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/elf_types.h>

#define SELFTEST_PREFIX "selftest_"

struct elf_symbol_info {

   void *vaddr;
   u32 size;
   const char *name;
};

void call_kernel_global_ctors(void);
uptr find_addr_of_symbol(const char *searched_sym);
const char *find_sym_at_addr(uptr vaddr, ptrdiff_t *offset, u32 *sym_size);
const char *find_sym_at_addr_safe(uptr vaddr, ptrdiff_t *offset, u32 *sym_size);

int foreach_symbol(int (*cb)(struct elf_symbol_info *, void *), void *arg);
