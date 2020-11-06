/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/paging.h>

#define ELF_RAW_HEADER_SIZE   128

struct locked_file; /* forward declaration */

struct elf_program_info {

   pdir_t *pdir;           // The pdir used for the program
   void *entry;            // The address of program's entry point
   void *stack;            // The initial value of the stack pointer
   void *brk;              // The first invalid vaddr (program break)
   struct locked_file *lf; // ELF's file lock (can be NULL)
   bool wrong_arch;        // The ELF is compiled for the wrong arch
   bool dyn_exec;          // The ELF is a dynamic executable (not supported)
};

/*
 * Loads an ELF program in memory.
 *
 * `filepath`: IN arg, the path of the ELF file to load.
 *
 * `header_buf`: IN arg, the address of a buffer where to store the first
 * `ELF_RAW_HEADER_SIZE` bytes of the file at `filepath`.
 *
 * 'pinfo': OUT arg, essential info about the loaded program.
 */
int load_elf_program(const char *filepath,
                     char *header_buf,
                     struct elf_program_info *pinfo);
