# SPDX-License-Identifier: BSD-2-Clause

.intel_syntax noprefix

#define ASM_FILE 1

#include <tilck/common/config.h>
#include <tilck/kernel/arch/i386/asm_defs.h>
#include <multiboot.h>

.code32

.section bss

.global kernel_initial_stack
.global vsdo_like_page

.comm kernel_initial_stack, KERNEL_INITIAL_STACK_SIZE, 4096
.comm vsdo_like_page, 4096, 4096

.section .text

.global _start

#define MULTIBOOT_FLAGS      (MULTIBOOT_PAGE_ALIGN |   \
                              MULTIBOOT_MEMORY_INFO)

#define PAGE_DIR_PADDR ((offset page_size_buf) - KERNEL_BASE_VA)

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

   .long 0 /* mode_type: unset */
   .long 0 /* cols: unset */
   .long 0 /* rows: unset */
   .long 0 /* color depth: unset */

/* End multiboot header */

multiboot_entry:

   /* Clear the direction flag */
   cld

   /*
    * Before jump to kernel, we have to setup a basic paging in order to map
    * the first 4-MB both at 0 and at +KERNEL_BASE_VA. Using 4-MB pages.
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
   mov ecx, 1 /* present */ | 2 /* RW */ | (1 << 7) /* 4-MB page */

   mov edx, PAGE_DIR_PADDR

   # Identity map the first 4 MB
   mov [edx], ecx

   # Map the first 4 MB at KERNEL_BASE_VA
   mov [edx + (KERNEL_BASE_VA >> 20)], ecx

   # Set the CR3 register
   mov cr3, edx

   mov ecx, cr4
   or ecx, 16      # bit 4, PSE (Page Size Extension). Allows 4-MB pages.
   or ecx, 128     # bit 7, PGE (Page Global Enabled)
   mov cr4, ecx

   mov ecx, cr0
   or ecx, 0x80000000 # paging ON
   or ecx, 0x00010000 # WP ON (write protect for supervisor)
   mov cr0, ecx       # enable paging!


   mov ecx, offset .next_step
   jmp ecx        # Jump to next instruction using the high virtual address.

                  # This is necessary since here the EIP is still a physical
                  # address, while in the kernel the physical identity mapping
                  # is removed. We need to continue using high (+3 GB)
                  # virtual addresses. The trick works because this file is
                  # part of the kernel ELF binary where the ORG is set to
                  # 0xC0100000 (KERNEL_BASE_VA + KERNEL_PADDR).

.next_step:
   mov esp, offset kernel_initial_stack + KERNEL_INITIAL_STACK_SIZE - 4

   push ebx    # 2st argument: multiboot information structure
   push eax    # 1nd argument: multiboot magic
   call kmain  # Now call kernel's kmain() which uses
               # KERNEL_BASE_VA + KERNEL_PADDR as ORG

END_FUNC(_start)

