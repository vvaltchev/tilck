/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <elf.h>

#if defined(USE_ELF32) && defined(USE_ELF64)
   #error Invalid configuration: USE_ELF32 and USE_ELF64 both defined.
#endif

#if defined(USE_ELF32) || (defined(__i386__) && !defined(USE_ELF64))

   typedef Elf32_Addr Elf_Addr;
   typedef Elf32_Ehdr Elf_Ehdr;
   typedef Elf32_Phdr Elf_Phdr;
   typedef Elf32_Shdr Elf_Shdr;
   typedef Elf32_Sym Elf_Sym;

#elif defined(USE_ELF64) || ((defined(__x86_64__) || defined(__aarch64__)) \
                             && !defined(USE_ELF32))

   typedef Elf64_Addr Elf_Addr;
   typedef Elf64_Ehdr Elf_Ehdr;
   typedef Elf64_Phdr Elf_Phdr;
   typedef Elf64_Shdr Elf_Shdr;
   typedef Elf64_Sym Elf_Sym;

#else

#error Unknown architecture

#endif
