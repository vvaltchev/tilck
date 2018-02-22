
.intel_syntax noprefix

.section .text
.global _start
.global main

_start:
main:

   # Zero-out the BSS

   mov eax, offset __bss_start
   mov ebx, offset __bss_end
   xor ecx, ecx

.loop:

   mov [eax], ecx
   add eax, 4

   cmp eax, ebx
   jle .loop

   # Jump to main
   jmp bootloader_main
