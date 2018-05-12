
.intel_syntax noprefix

#include <common/config.h>

.global _start

.section .text
.code64

_start:

   cli

   lea ecx, [gdtr+2]
   mov dword ptr [ecx], offset gdt32
   lgdt [gdtr]


   # With NASM that was simply: jmp far [far_ptr]
   rex.W jmp FWORD PTR [far_ptr]

far_ptr:
   .quad compat32
   .word 0x08 # cs

.code32
compat32:

   # Here we are in a compatibility 32 bit mode of long mode
   # so, we're using 32 bit, but we're still (kind of) in long mode.

   mov ax, 0x10
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax
   mov ss, ax
   mov esp, 0xFFF0

   # Disable Paging to get out of Long Mode
   mov ecx, cr0 # Read CR0.
   and ecx, 0x7fffffff # disable paging
   mov cr0, ecx # Write CR0.

   # Deactivate Long Mode
   mov ecx, 0xC0000080 # EFER MSR number.
   rdmsr # Read EFER.
   btc eax, 8 # Set LME=0.
   wrmsr # Write EFER.

   mov eax, cr4
   and eax, 0xFFFFFFDF # disable PAE
   mov cr4, eax

   xor eax, eax
   mov cr3, eax # it's a good practice to reset cr3, as well

   jmp 0x08:pm32

pm32:
   # Here we are in the proper "32-bit protected mode".

   # Now jump to kernel's entry point
   mov eax, 0x2BADB002 # MULTIBOOT_BOOTLOADER_MAGIC
   jmp 0x08:(KERNEL_PADDR+0x1000)

   # --- we should hever get past this point ---

# GDT selectors:
#
# [bits 15 .. 3] | [bit 2]  | [bits 1..0]
#   GDT index    | GDT/LDT  |    RPL
#
# GDT/LDT: GDT=0, LDT=1
# RPL: requested priv. level
# 00 = ring 0
# 01 = ring 1
# 10 = ring 2
# 11 = ring 3

# for gdt:
# http://wiki.osdev.org/Global_Descriptor_Table

gdt32:
   .byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 # sel 0x00.
   .byte 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x9A, 0xCF, 0x00 # sel 0x08. 32-bit code
   .byte 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x92, 0xCF, 0x00 # sel 0x10. 32-bit data


# GDTR format
# size-1 [ 2 bytes ], gdt addr [ 4 bytes ]

gdtr:
   .word 0x0023
   .long 0x00000000


.end
