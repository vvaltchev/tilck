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

#define PML4_PADDR ((offset page_size_buf) - KERNEL_BASE_VA)
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

   /* Save multiboot information for now and restore it later */
   mov edx, ebx    # multiboot information structure
   mov esi, eax    # multiboot magic

   /*
    * Long mode is supported if:
    *
    * 1. CPUID is supported
    * 2. CPUID Extended Functions are available
    * 3. CPUID.80000001H:EDX.[29] == 1
    */

   /*
    * 1. CPUID is supported:
    *
    * Assume that CPUID is supported */

   /*
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
    * So, each table requires
    * 512 * 8 = 4096 bytes = 0x1000.
    *
    * PML4T is located at PML4_PADDR and subsequent
    * tables at PML4_PADDR + 0x1000, PML4_PADDR + 0x2000
    * and PML4_PADDR + 0x3000.
    *
    * The first two bits of an entry in a table specify
    * PRESENT and R/W respectively. Hence, the `3` is used.
    * (Intel Manual Vol. 3: Figure 4-11)
    *
    * PT intentionally left since we are using 2MB pages.
    * See identity mapping for more information.
    *
    * This is effectively:
    * CR3 -> PML4T -> PDPT -> PDT -> 2MB page
    */

   mov edx, PML4_PADDR  # edx points to PML4
   # Set the first entry of PML4 (0 - 512 GB)
   mov dword ptr [edx], PML4_PADDR + 0x1003
   add edx, 0x1000      # edx points to PDPT
   # Set the first entry of PDPT (0 - 1 GB)
   mov dword ptr [edx], PML4_PADDR + 0x2003
   add edx, 0x1000      # edx points to PDT

   /*
    * Set the first 4 entries of PDT (0 - 8 MB)
    *
    * Use the first 4 2MB pages for identity mapping.
    * To use 2MB pages, set Bit 7 of an entry of PDT.
    * Note: there are no 4MB pages in x86-64,
    *       only 4KB, 2MB and 1GB pages are available.
    * (Intel Manual Vol. 3: Figure 4-11)
    */
   mov ebx, 0x3
   bts ebx, 7
   mov ecx, 4 # how many times to loop for

.set_entry:
   mov dword ptr [edx], ebx
   add ebx, 0x200000 # 2 MB
   add edx, 8
   loop .set_entry

   /*
    * Map the first 8MB also at KERNEL_BASE_VA (-2GB).
    * This will contain the kernel.
    */

   mov edx, PML4_PADDR
   # Set the entry specified by bits 47-39 of KERNEL_BASE_VA (-2GB)
   # The 9 bits specify the nth entry in the table
   # Multiply it by 8 (size of an entry) to get the offset
   # from the base of the table
   # So it becomes:
   #    (KERNEL_BASE_VA & 0xFF8000000000) >> 39 // to get 9 bits
   #    (KERNEL_BASE_VA & 0xFF8000000000) >> 36 // multiply by 8
   mov dword ptr [edx + ((KERNEL_BASE_VA & 0xFF8000000000) >> 36)], PML4_PADDR + 0x1003
   add edx, 0x1000      # edx points to PDPT
   # Set the entry specified by bits 38-30 of KERNEL_BASE_VA (-2GB)
   mov dword ptr [edx + ((KERNEL_BASE_VA & 0x007F80000000) >> 27)], PML4_PADDR + 0x2003
   # Now there is no need to set entries in PDT because that is
   # already set up by the identity mapping part

   /* Map the first 8MB also at BASE_VA (+16TB) */

   mov edx, PML4_PADDR
   # Set the entry specified by bits 47-39 of BASE_VA (+16TB)
   mov dword ptr [edx + ((BASE_VA & 0xFF8000000000) >> 36)], PML4_PADDR + 0x1003
   add edx, 0x1000      # edx points to PDPT
   # Set the entry specified by bits 38-30 of BASE_VA (+16TB)
   mov dword ptr [edx + ((BASE_VA & 0x007F80000000) >> 27)], PML4_PADDR + 0x2003
   # Now there is no need to set entries in PDT because that is
   # already set up by the identity mapping part

   # Set cr3 register to the physical address of PML4
   mov edx, PML4_PADDR
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
   btr eax, 12 # LA57 is bit 12
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
   push 0x08 # SI=1, TI=0, RPL=00
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

   # D=0, L=1, P=1, DPL=00, S=1, Bit 11=1, C=0, R=1
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

   # P=1, S=1, W=1, Bit 11=0
   .quad 0x0000920000000000
gdt_end:

gdtr:
   /*
    * GDTR contains 2 fields:
    * 1. Limit (2 bytes): size of gdt in bytes
    * 2. Base  (8 bytes): starting byte address of gdt
    *                     in virtual memory space
    */

   .word gdt_end - gdt - 1 # subtract 1 because limit + base
                           # should specify the address of the
                           # last valid byte in gdt
   .quad offset gdt - KERNEL_BASE_VA

no_long_mode:
   hlt

.code64

trampoline:
   # 0x10 is ds segment selector
   # SI=2, TI=0, RPL=00
   mov ax, 0x10
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax
   mov ss, ax

   # Perform a near jump to 64 bit address
   # to enter 64 bit long mode
   movabs rax, offset long_mode
   jmp rax

long_mode:
   cli
   mov rsp, offset kernel_initial_stack + ASM_KERNEL_STACK_SZ - 16
   call kmain  # Now call kernel's kmain() which uses
               # KERNEL_VADDR as ORG

END_FUNC(_start)
