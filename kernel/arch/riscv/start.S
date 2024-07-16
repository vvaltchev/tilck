# SPDX-License-Identifier: BSD-2-Clause


#define ASM_FILE 1

#include <tilck_gen_headers/config_global.h>
#include <tilck_gen_headers/config_kernel.h>
#include <tilck_gen_headers/config_boot.h>
#include <tilck/kernel/arch/riscv/asm_defs.h>

#include <multiboot.h>

.section .bss
.global kernel_initial_stack
.comm kernel_initial_stack, ASM_KERNEL_STACK_SZ, 8192

.section .text.start
.global _start

#define RISCV_IMAGE_MAGIC   "RISCV\0\0\0"
#define RISCV_IMAGE_MAGIC2   "RSC\x05"

#define RISCV_HEADER_VERSION_MAJOR 0
#define RISCV_HEADER_VERSION_MINOR 2

#define RISCV_HEADER_VERSION (RISCV_HEADER_VERSION_MAJOR << 16 | \
               RISCV_HEADER_VERSION_MINOR)

FUNC(_start):
   /*
    * Image header expected by Linux boot-loaders. The image header data
    * structure is described in image.h.
    * Do not modify it without modifying the structure and all bootloaders
    * that expects this header format!!
    */
   /* jump to start kernel */
   j _start_kernel
   /* reserved */
   .word 0
   .balign 8
#if __riscv_xlen == 64
   /* Image load offset(2MB) from start of RAM */
   .dword 0x200000
#else
   /* Image load offset(4MB) from start of RAM */
   .dword 0x400000
#endif
   /*
    * Effective size of kernel image , Note tilck
    * has an elf header(0x1000 bytes long) before the _start symbol
    */
   .dword _end - _start + 0x1000
   .dword 0
   .word RISCV_HEADER_VERSION
   .word 0
   .dword 0
   .ascii RISCV_IMAGE_MAGIC
   .balign 4
   .ascii RISCV_IMAGE_MAGIC2
   .word 0

_start_kernel:
   csrw CSR_SIE, zero
   csrw CSR_SIP, zero
   csrw CSR_SSCRATCH, zero

   j _start_kernel

END_FUNC(_start)
