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

   #define ELF_ST_BIND(val)         ELF32_ST_BIND (val)
   #define ELF_ST_TYPE(val)         ELF32_ST_TYPE (val)
   #define ELF_ST_INFO(bind, type)  ELF32_ST_INFO ((bind), (type))
   #define ELF_ST_VISIBILITY(o)     ELF32_ST_VISIBILITY (o)

#elif defined(USE_ELF64) || ((defined(__x86_64__) || defined(__aarch64__) \
                              || defined(__riscv64)) && !defined(USE_ELF32))

   typedef Elf64_Addr Elf_Addr;
   typedef Elf64_Ehdr Elf_Ehdr;
   typedef Elf64_Phdr Elf_Phdr;
   typedef Elf64_Shdr Elf_Shdr;
   typedef Elf64_Sym Elf_Sym;

   #define ELF_ST_BIND(val)         ELF64_ST_BIND (val)
   #define ELF_ST_TYPE(val)         ELF64_ST_TYPE (val)
   #define ELF_ST_INFO(bind, type)  ELF64_ST_INFO ((bind), (type))
   #define ELF_ST_VISIBILITY(o)     ELF64_ST_VISIBILITY (o)

#else

#error Unknown architecture

#endif
