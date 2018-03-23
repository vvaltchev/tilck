
#pragma once
#include <common_defs.h>
#include <paging.h>

/*
 * Loads the ELF file 'filepath' in memory.
 *
 * pdir_ref is IN/OUT: in case *pdir_ref is NOT NULL, the supplied pdir will
 * be used. Otherwise, *pdir_ref will be a clone of kernel's pdir. Also, this
 * function will call set_page_directory(*pdir_ref).
 *
 * 'entry': OUT arg, the address of program's entry point.
 * 'stack_addr': OUT arg, the initial value of the stack pointer.
 *
 * TODO: this function needs a lot of work. For example, all the "error
 * handling" today is just done with VERIFY() which, expect for early testing,
 * makes really no sense. All the error paths have to explicitly handled with
 * return codes. In no case an invalid ELF file, a read error or an out of
 * memory error should make the kernel to panic.
 */
int load_elf_program(const char *filepath,
                     page_directory_t **pdir_ref,
                     void **entry,
                     void **stack_addr);
