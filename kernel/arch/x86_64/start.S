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

#define PML4 ((offset page_size_buf) - KERNEL_BASE_VA)
#define PDPT0 ((offset early_pdpt0) - KERNEL_BASE_VA)
#define PDPT1 ((offset early_pdpt1) - KERNEL_BASE_VA)
#define PDT0  ((offset early_pdt0) - KERNEL_BASE_VA)

#define FL_PRESENT      (1 << 0)
#define FL_RW           (1 << 1)
#define FL_PAGE_SIZE    (1 << 7)

#define MAKE_2MB_PAGE(paddr) \
   (FL_PRESENT | FL_RW | FL_PAGE_SIZE | (paddr))

/*
 * Generic macro for making a table entry in both PDPT and PDT.
 * HACK: use '+' instead of '|' because the paddr is a symbol and we cannot
 * emit a symbol relocation that needs to be calculated with an OR operation.
 */
#define MAKE_ENTRY(paddr) \
   (paddr + (FL_PRESENT | FL_RW))

/* Offset of an entry in the PML4 table */
#define PML4E_OFF(va) \
   (8 * (((va) & 0xFF8000000000) >> 39))

/* Offset of an entry in the PDPT table */
#define PDPTE_OFF(va) \
   (8 * (((va) & 0x007F80000000) >> 30))

/* Offset of an entry in the PDT table */
#define PDTE_OFF(va) \
   (8 * (((va) & 0xFF80000) >> 21))

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

   /* Store the multiboot information in the first two args of main() */
   mov edi, eax    # multiboot magic
   mov esi, ebx    # multiboot information structure

   /*
    * Long mode is supported if:
    *
    * 1. CPUID is supported
    * 2. CPUID Extended Functions are available
    * 3. CPUID.80000001H:EDX.[29] == 1
    */

   /*
    * 1. Assume that CPUID is supported.
    *
    * 2. CPUID Extended Functions are available:
    *
    * Maximum Input Value for Extended Function CPUID Information
    * is given by CPUID.80000000H:EAX
    * (Intel Manual Vol. 2: Table 3-8)
    *
    * So if any extended function > 80000000H does not
    * exist then Long mode is not supported.
    */
   mov eax, 0x80000000
   cpuid
   cmp eax, 0x80000000
   jbe no_long_mode

   /*
    * 3. CPUID.80000001H:EDX.[29] == 1:
    *
    * Bit 29: Intel(R) 64 Architecture available if 1
    * (Intel Manual Vol. 2: Table 3-8)
    */
   mov eax, 0x80000001
   cpuid
   bt edx, 29
   jnc no_long_mode

   /*
    * To switch to Long mode:
    *
    * 1. Enable Long mode (LME)
    * 2. Enable Physical Address Extension (PAE)
    * 3. Set up Paging
    * 4. Enable Paging (PG)
    *
    * (AMD Manual Vol. 2: Figure 1-6)
    */

   /*
    * 1. Enable Long mode (LME):
    *
    * MSR IA32_EFER (C0000080H)
    * Bit 8: IA-32e Mode Enable: IA32_EFER.LME (R/W)
    *        Enables IA-32e mode operation
    * (Intel Manual Vol. 4: Table 2-2)
    */
   mov ecx, 0xc0000080
   rdmsr
   bts eax, 8
   wrmsr

   /*
    * 2. Enable Physical Address Extension (PAE):
    *
    * (Bit 5) CR4.PAE = 1
    * (Intel Manual Vol. 3: 4.1)
    */
   mov eax, cr4
   bts eax, 5
   mov cr4, eax

   /*
    * 3. Set up Paging:
    *
    * We use 4-level paging:
    * CR3 -> PML4T -> PDPT -> PDT -> PT
    * (Intel Manual Vol. 3: 4.5)
    *
    * Each table (PML4T, PDPT, PDT, PT) has
    * 512 entries and each entry is 8 bytes.
    *
    * Since we skip the 4 KB pages here, this is effectively:
    * CR3 -> PML4T -> PDPT -> PDT -> 2MB page
    */

   // First, build a PDT (Page Directory) with the first 8 MB mapped to 0-8 MB.
   mov edx, PDT0
   mov dword ptr [edx + PDTE_OFF(0 * MB)], MAKE_2MB_PAGE(0 * MB)
   mov dword ptr [edx + PDTE_OFF(2 * MB)], MAKE_2MB_PAGE(2 * MB)
   mov dword ptr [edx + PDTE_OFF(4 * MB)], MAKE_2MB_PAGE(4 * MB)
   mov dword ptr [edx + PDTE_OFF(6 * MB)], MAKE_2MB_PAGE(6 * MB)

   // Second, build a PDPT (Page-Directory-Pointer Table) that will map in
   // its first entry (0 - 1 GB) our PDT for identity mapping. We'll later
   // refer this PDPT in multiple entries in the PML4.
   mov edx, PDPT0
   mov dword ptr [edx + PDPTE_OFF(0)], MAKE_ENTRY(PDT0)
   mov dword ptr [edx + PDPTE_OFF(BASE_VA)], MAKE_ENTRY(PDT0)

   // Third, build another PDPT for the kernel mappings. Reuse PDT0, since
   // we're still assuming that the kernel is physically mapped at +1 MB.
   mov edx, PDPT1
   mov dword ptr [edx + PDPTE_OFF(KERNEL_BASE_VA)], MAKE_ENTRY(PDT0)

   // Fourth, build a PML4 referring the PDPT0 for two virtual addresses
   // [0 (early identity mapping) and BASE_VA] and PDPT1 at KERNEL_BASE_VA.
   mov edx, PML4
   mov dword ptr [edx + PML4E_OFF(0)], MAKE_ENTRY(PDPT0)
   mov dword ptr [edx + PML4E_OFF(BASE_VA)], MAKE_ENTRY(PDPT0)
   mov dword ptr [edx + PML4E_OFF(KERNEL_BASE_VA)], MAKE_ENTRY(PDPT1)

   // Set cr3 register to the physical address of PML4
   mov cr3, edx

   /*
    * 4. Enable Paging (PG):
    *
    * (Bit 31) CR0.PG = 1
    * This requires protection to be enabled (CR0.PE = 1)
    * Since we are in protected mode, CR0.PE is
    * already 1.
    *
    * The values of CR4.PAE, CR4.LA57, and IA32_EFER.LME
    * determine which paging mode is used.
    * We use 4-level paging, for which the values of the
    * bits should be 1, 0 and 1 respectively.
    * (Intel Manual Vol. 3: 4.1, 4.1.1)
    */
   mov eax, cr4
   btr eax, 12 // LA57 is bit 12
   mov cr4, eax

   mov eax, cr0
   bts eax, 31
   mov cr0, eax

   /*
    * We are now in Compatibility submode of Long mode.
    * To enter 64-bit submode of Long mode,
    * Set up GDT, set CS.L = 1 and jump to
    * 64-bit code segment.
    * (AMD Manual Vol. 2: Figure 1-6)
    */

   mov eax, offset gdtr - KERNEL_BASE_VA
   lgdt [eax]

   /*
    * Set the Code Segment Selector
    *
    * 15-3  2  1-0
    * +----+---+----+
    * |SI  |TI |RPL |
    * +----+---+----+
    *
    * SI: Selector Index. Index of entry in the
    *     descriptor table. 1 for CS
    * TI: Table Indicator. 0 for GDT, 1 for LDT
    * RPL: Requestor Privilege Level
    *
    * (AMD Manual Vol. 2: section 4.5.1)
    */

   /*
    * We cannot directly jump to 64 bit address
    * because there is no jmp insruction in 32 bit that
    * accepts a 64 bit address.
    *
    * So instead, do a far jump to 64 bit code
    * segment (trampoline) running at a 32 bit
    * address. Then from there do a near jump
    * to 64 bit address.
    */
   mov eax, offset trampoline - KERNEL_BASE_VA
   push 0x08 // SI=1, TI=0, RPL=00
   push eax
   retf

gdt:
   /* Null Descriptor */
   .quad 0x0

   /*
    * Code Segment Descriptor
    *
    * First double-word is ignored.
    * Second double-word:
    *
    *  31-23    22  21  20-16   15  14-13  12  11  10  9   8-0
    * +--------+---+---+-------+---+------+---+---+---+---+----+
    * |Ign     |D  |L  |Ign    |P  |DPL   |S  |1  |C  |R  |Ign |
    * +--------+---+---+-------+---+------+---+---+---+--------+
    *
    * Ign : Ignored
    * D   : Default. In long mode, D=0 means default operand
    *       size of 32 and default address size of 64. D=1 is reserved.
    * L   : 64-bit flag
    * P   : Present
    * DPL : Descriptor Privilege Level. 4 levels, typically only 2 are
    *       used - 00 (highest) and 11 (lowest)
    * S   : System flag. 1 for NON-system segment and
    *       0 for system segment. (Not intuitive, be careful)
    * C   : Conforming
    * R   : Readable
    *
    * Note: Bit 11 is 1 for code segment, and 0
    *       for data segment.
    *
    * (Intel Manual Vol. 3, section 5.2.1)
    * (AMD Manual Vol. 2, section 4.8)
    */

   // D=0, L=1, P=1, DPL=00, S=1, Bit 11=1, C=0, R=1
   .quad 0x00209A0000000000

   /*
    * Data Segment Descriptor
    *
    * First double-word is ignored.
    * Second double-word:
    *
    *  31-16  15  14-13  12  11  14-0
    * +------+---+------+---+---+-----+
    * |Ign   |P  |Ign   |S  |0  |Ign  |
    * +------+---+------+---+---+-----+
    *
    * Note: There is bit 9 (W: Writable) which is
    *       ignored but QEMU still requires it. Otherwise,
    *       there is crash when loading ss.
    *
    * (Intel Manual Vol. 3, section 5.2.1)
    * (AMD Manual Vol. 2, section 4.8)
    */

   // P=1, S=1, W=1, Bit 11=0
   .quad 0x0000920000000000
gdt_end:

gdtr:
   /*
    * GDTR contains 2 fields:
    * 1. Limit (2 bytes): size of gdt in bytes
    * 2. Base  (8 bytes): starting byte address of gdt
    *                     in virtual memory space
    */

   .word gdt_end - gdt - 1 // subtract 1 because limit + base
                           // should specify the address of the
                           // last valid byte in gdt
   .quad offset gdt - KERNEL_BASE_VA

no_long_mode:
   hlt

.code64

trampoline:
   // 0x10 is DS segment selector
   // SI=2, TI=0, RPL=00
   mov ax, 0x10
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax
   mov ss, ax

   // Perform a near jump to 64 bit address
   // to enter 64 bit long mode
   movabs rax, offset long_mode
   jmp rax

long_mode:
   cli
   mov rsp, offset kernel_initial_stack + ASM_KERNEL_STACK_SZ - 16
   call kmain

END_FUNC(_start)

# Tell GNU ld to not worry about us having an executable stack
.section .note.GNU-stack,"",@progbits
