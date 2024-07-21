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
.global _boot_cpu_hartid

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

   /* Disable FPU */
   li t0, SR_FS
   csrc sstatus, t0

   /* The first hart to get the lottery continues to execute */
   lla   a6, _boot_lottery
   li   a7, 1
   amoadd.w a6, a7, (a6)
   bnez   a6, _wait_forerver

   /* clear bss */
   la a3, __bss_start
   la a4, __bss_stop
   ble a4, a3, clear_bss_done
clear_bss:
   REG_S zero, (a3)
   add a3, a3, RISCV_SZPTR
   blt a3, a4, clear_bss
clear_bss_done:

   /* Setup temporary trap handler */
   lla   a5, _early_fault
   csrw  CSR_STVEC, a5

   mv s0, a0   /* Save hart ID */
   mv s1, a1   /* Save DTB physical address */
   la a2, _boot_cpu_hartid
   REG_S a0, (a2)

   la sp, kernel_initial_stack + ASM_KERNEL_STACK_SZ
   call sbi_init

   mv a0, s1
   call init_early_mapping

   call parse_fdt
   mv s2, a0   /* Save multiboot info structure physical address */

   la a0, page_size_buf
   li a1, SATP_MODE
   srl a0, a0, 12
   or a0, a0, a1

   la a1, kernel_va_pa_offset
   REG_L a1, (a1)

   /* Enable paging */
   sfence.vma
   csrw CSR_SATP, a0

   /* Calculate return address */
   la ra, .next_step
   add ra, ra, a1

   /* Return to .next_step using high virtual address */
   ret

.next_step:

   call early_get_cpu_features

   /* Enable access to user memory */
   li t0, SR_SUM
   csrs CSR_SSTATUS, t0

   la sp, kernel_initial_stack + ASM_KERNEL_STACK_SZ
   mv a1, s2   /* 2st argument: multiboot information structure */
   li a0, MULTIBOOT_BOOTLOADER_MAGIC   /* 1nd argument: multiboot magic */
   call kmain

.align 2
_wait_forerver:
   /* We lack SMP support, so park this hart */
   wfi
   j _wait_forerver

.align 3
_early_fault:
   wfi
   j   _early_fault

END_FUNC(_start)

.section .data
.align 3
_boot_lottery:
   RISCV_PTR   0
_boot_cpu_hartid:
   .word   0
