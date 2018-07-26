
.intel_syntax noprefix

#include <tilck/common/config.h>
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

.global switch_to_pm32_and_jump_to_kernel

.section .text
.code64


switch_to_pm32_and_jump_to_kernel:

   # arg1 in RDI: mbi
   # arg2 in RSI: kernel's entry point

   cli

   lea rcx, [rip + gdtr + 2]     # rcx = gdtr + 2 (using rip addressing)
   lea rdx, [rip + gdt32]        # rdx = gdt32 (using rip addressing)
   mov [rcx], rdx                # *rcx = rdx [gdtr->ptr = gdt32]
   lgdt [rip + gdtr]             # lgdt [gdtr]

   lea rdx, [rip + compat32]
   push 0x08
   push rdx
   rex.W retf # perform a 32-bit far ret (=> far jmp to 0x08:compat32)

.code32
compat32:

   # Here we are in a compatibility 32 bit mode of long mode
   # so, we're using 32 bit, but we're still (kind of) in long mode.

   mov cx, 0x10
   mov ds, cx
   mov es, cx
   mov fs, cx
   mov gs, cx
   mov ss, cx
   mov esp, 0xFFF0

   # Disable Paging to get out of Long Mode
   mov ecx, cr0 # Read CR0.
   and ecx, 0x7fffffff # disable paging
   mov cr0, ecx # Write CR0.

   # Deactivate Long Mode

   push eax

   mov ecx, 0xC0000080 # EFER MSR number.
   rdmsr # Read EFER.
   btc eax, 8 # Set LME=0.
   wrmsr # Write EFER.

   pop eax

   mov ecx, cr4
   and ecx, 0xFFFFFFDF # disable PAE
   mov cr4, ecx

   xor ecx, ecx
   mov cr3, ecx # it's a good practice to reset cr3, as well

   mov eax, MULTIBOOT_BOOTLOADER_MAGIC
   mov ebx, edi # edi: arg1 = mbi

   push 0x08
   push esi # esi: arg2 = kernel's entry point
   # Jump to kernel's entry point and enter in proper "32-bit protected mode"
   retf

.align 8

gdt32:
   .byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 # sel 0x00.
   .byte 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x9A, 0xCF, 0x00 # sel 0x08. 32-bit code
   .byte 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x92, 0xCF, 0x00 # sel 0x10. 32-bit data

gdtr:
   .word 0x23
   .quad 0

.end
