/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/string_util.h>
#include <elf.h>

/*
 * Simple ELF loader used by both the EFI bootloader and the legacy one.
 * It assumes that the whole ELF file is loaded in memory at the address
 * pointed by `elf`. It returns the physical address of executable's entry
 * point, even when `header->e_entry` is a virtual address (see the code).
 *
 * NOTE: the code is in a source-header (.c.h) in order to avoid it to be
 * compiled and linked in binaries that don't need it, with the whole package
 * of C files in common/.
 */

void *simple_elf_loader(void *elf)
{
   Elf32_Ehdr *header = elf;
   Elf32_Phdr *phdr = (Elf32_Phdr *)((char*)header + header->e_phoff);
   void *entry;

   /* Just set the entry in case the search with IN_RANGE() below fails */
   entry = TO_PTR(header->e_entry);

   for (int i = 0; i < header->e_phnum; i++, phdr++) {

      // Ignore non-load segments.
      if (phdr->p_type != PT_LOAD)
         continue;

      bzero(TO_PTR(phdr->p_paddr), phdr->p_memsz);
      memcpy(TO_PTR(phdr->p_paddr),
             (char *) header + phdr->p_offset,
             phdr->p_filesz);

      if (IN_RANGE(header->e_entry,
                   phdr->p_vaddr,
                   phdr->p_vaddr + phdr->p_filesz))
      {
         /*
          * If e_entry is a vaddr (address >= KERNEL_BASE_VA), we need to
          * calculate its paddr because here paging is OFF. Therefore,
          * compute its offset from the beginning of the segment and add it
          * to the paddr of the segment.
          */

         entry = TO_PTR(phdr->p_paddr + (header->e_entry - phdr->p_vaddr));
      }
   }

   return entry;
}
