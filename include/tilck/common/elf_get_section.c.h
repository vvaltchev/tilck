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

static Elf_Shdr *
elf_get_section(Elf_Ehdr *h, const char *section_name)
{
   Elf_Shdr *sections = (Elf_Shdr *) ((char *)h + h->e_shoff);
   Elf_Shdr *section_header_strtab = sections + h->e_shstrndx;

   for (uint32_t i = 0; i < h->e_shnum; i++) {

      Elf_Shdr *s = sections + i;
      char *name = (char *)h + section_header_strtab->sh_offset + s->sh_name;

      if (!strcmp(name, section_name)) {
         return s;
      }
   }

   return NULL;
}
