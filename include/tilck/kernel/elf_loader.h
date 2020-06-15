/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/paging.h>

#define ELF_RAW_HEADER_SIZE   128

struct locked_file; /* forward declaration */

struct elf_program_info {

   pdir_t *pdir;           // the pdir used for the program
   void *entry;            // the address of program's entry point
   void *stack;            // the initial value of the stack pointer
   void *brk;              // the first invalid vaddr (program break)
   struct locked_file *lf; // ELF's file lock (can be NULL)
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
