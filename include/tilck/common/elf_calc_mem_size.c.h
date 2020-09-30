/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/elf_types.h>
#include <tilck/common/utils.h>
#include <elf.h>

/*
 * NOTE[1]: the code is in a source-header (.c.h) in order to avoid it to be
 * compiled and linked in binaries that don't need it, with the whole package
 * of C files in common/.
 *
 * NOTE[2]: this file uses Tilck's generic ELF types (Elf_Ehdr instead of
 * Elf_Ehdr32 etc.). In your code, you might need to define USE_ELF32 or
 * USE_ELF64 to work with ELF files having bitness different than your NBITS.
 */

size_t
elf_calc_mem_size(Elf_Ehdr *h)
{
   Elf_Phdr *phdrs = (Elf_Phdr *)((char*)h + h->e_phoff);
   Elf_Addr min_pbegin = 0;
   Elf_Addr max_pend = 0;

   for (uint32_t i = 0; i < h->e_phnum; i++) {

      Elf_Phdr *p = phdrs + i;
      Elf_Addr pend = pow2_round_up_at(p->p_vaddr + p->p_memsz, p->p_align);

      if (i == 0 || p->p_vaddr < min_pbegin)
         min_pbegin = p->p_vaddr;

      if (pend > max_pend)
         max_pend = pend;
   }

   return max_pend - min_pbegin;
}
