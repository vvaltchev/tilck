# SPDX-License-Identifier: BSD-2-Clause

.intel_syntax noprefix

#define ASM_FILE 1

#include <tilck_gen_headers/config_global.h>
#include <tilck_gen_headers/config_kernel.h>
#include <tilck_gen_headers/config_boot.h>
#include <tilck/kernel/arch/i386/asm_defs.h>
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
    * Before jump to kernel, we have to setup a basic paging in order to map
    * the first 4-MB both at 0 and at +BASE_VA. Using 4-MB pages.
    * NOTE: the registers EAX and EBX cannot be used since they contain
    * multiboot information!
    */

   mov edi, PAGE_DIR_PADDR
   xor ecx, ecx

   # Zero our page directory
1:
   mov [edi], ecx
   add edi, 4
   cmp edi, PAGE_DIR_PADDR + 4096
   jne 1b

   # Set our flags (note the absence of a paddr since it is 0)
   # mov ecx, 1 /* present */ | 2 /* RW */ | (1 << 7) /* 4-MB page */

   mov edx, PAGE_DIR_PADDR

   # Identity map the first 8 MB: that's necessary in order to avoid crashing
   # due to a page fault once we execute: mov cr3, edx, below.
   #
   # TODO: replace the 8 * MB fixed mapping with a well-defined constant
   # and use the GAS macros to implement a loop to map an abitrary-sized
   # mapping (aligned at BIG_PAGE_SIZE, of course).
   #
   mov dword ptr [edx + 0], MAKE_BIG_PAGE(0)
   mov dword ptr [edx + 4], MAKE_BIG_PAGE(4 * MB)

   # Map the first 8 MB also at BASE_VA
   mov dword ptr [edx + (BASE_VA >> 20) + 0], MAKE_BIG_PAGE(0)
   mov dword ptr [edx + (BASE_VA >> 20) + 4], MAKE_BIG_PAGE(4 * MB)

#if !KRN32_LIN_VADDR

   # Map 8 MB starting at KERNEL_PADDR to KERNEL_VADDR
   mov dword ptr [edx + (KERNEL_BASE_VA >> 20) +  0], MAKE_BIG_PAGE(0)
   mov dword ptr [edx + (KERNEL_BASE_VA >> 20) +  4], MAKE_BIG_PAGE(4  * MB)

#endif

   # Make sure PAGING is disabled
   mov ecx, cr0
   and ecx, ~CR0_PG
   mov cr0, ecx

   mov ecx, cr4
   or ecx,   CR4_PSE   # enable PSE (Page Size Extension)
   or ecx,   CR4_PGE   # enable PGE (Page Global Enabled)
   and ecx, ~CR4_PAE   # disable PAE (Physical Address Extension)
   mov cr4, ecx

   # Set the CR3 register
   mov cr3, edx

   mov ecx, cr0
   or ecx, CR0_PG  # paging
   or ecx, CR0_WP  # write protect for supervisor
   mov cr0, ecx    # apply the changes!

   mov ecx, offset .next_step
   jmp ecx        # Jump to next instruction using the high virtual address.

                  # This is necessary since here the EIP is still a physical
                  # address, while in the kernel the physical identity mapping
                  # is removed. We need to continue using high (+3 GB)
                  # virtual addresses. The trick works because this file is
                  # part of the kernel ELF binary where the ORG is set to
                  # 0xC0100000 (KERNEL_VADDR).

.next_step:
   mov esp, offset kernel_initial_stack + ASM_KERNEL_STACK_SZ - 16

   push ebx    # 2st argument: multiboot information structure
   push eax    # 1nd argument: multiboot magic
   call kmain  # Now call kernel's kmain() which uses
               # KERNEL_VADDR as ORG

END_FUNC(_start)

# Tell GNU ld to not worry about us having an executable stack
.section .note.GNU-stack,"",@progbits
