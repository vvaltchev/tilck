/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/paging.h>

#define ELF_RAW_HEADER_SIZE   128

/*
 * Loads the ELF file 'filepath' in memory.
 *
 * header_buf: IN arg, the address of a buffer where to store the first
 * `ELF_RAW_HEADER_SIZE` bytes of the file at `filepath`.
 *
 * pdir_ref is IN/OUT: in case *pdir_ref is NOT NULL, the supplied pdir will
 * be used. Otherwise, *pdir_ref will be a clone of kernel's pdir. Also, this
 * function will call set_curr_pdir(*pdir_ref).
 *
 * 'entry': OUT arg, the address of program's entry point.
 * 'stack_addr': OUT arg, the initial value of the stack pointer.
 * 'brk': OUT arg, the first invalid vaddr (program break).
 */
int load_elf_program(const char *filepath,
                     char *header_buf,
                     pdir_t **pdir_ref,
                     void **entry,
                     void **stack_addr,
                     void **brk);
