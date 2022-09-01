# SPDX-License-Identifier: BSD-2-Clause

.intel_syntax noprefix

#define ASM_FILE 1

#include <tilck_gen_headers/config_global.h>
#include <tilck_gen_headers/config_kernel.h>
#include <tilck_gen_headers/config_boot.h>
#include <tilck/kernel/arch/x86_64/asm_defs.h>
#include <multiboot.h>


.code32

.section bss
.global kernel_initial_stack
.comm kernel_initial_stack, ASM_KERNEL_STACK_SZ, 4096

.section .text.start
.global _start

#define MULTIBOOT_FLAGS      (MULTIBOOT_PAGE_ALIGN |   \
                              MULTIBOOT_MEMORY_INFO |  \
                              MULTIBOOT_VIDEO_MODE)

#define PAGE_DIR_PADDR ((offset page_size_buf) - KERNEL_BASE_VA)
#define MAKE_BIG_PAGE(paddr) \
   (1 /* present */ | 2 /*RW*/ | (1 << 7) /* bigpage */ | (paddr))

FUNC(_start):

   jmp multiboot_entry

/* Multiboot header */

.align 4
   .long   MULTIBOOT_HEADER_MAGIC
   .long   MULTIBOOT_FLAGS
   .long   -(MULTIBOOT_HEADER_MAGIC+MULTIBOOT_FLAGS) /* checksum */

   .long 0
   .long 0
   .long 0
   .long 0
   .long 0

   .long 0                       /* mode_type: graphic */
   .long PREFERRED_GFX_MODE_W
   .long PREFERRED_GFX_MODE_H
   .long 32                      /* color depth */

/* End multiboot header */

multiboot_entry:

   /* Clear the direction flag */
   cld

   /*
    * We'll be in protected mode 32 here, so we'll have to enter in long mode.
    * TODO: enter in long mode and jump to the 64-bit kernel.
    */


END_FUNC(_start)
