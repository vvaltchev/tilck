
.intel_syntax noprefix

.section .text
.global _start

_start:

   # Zero-out the BSS

   mov eax, [__bss_start]
   mov ebx, [__bss_end]
   xor ecx, ecx

.loop:

   mov [eax], ecx
   add eax, 4

   cmp eax, ebx
   jle .loop

   # Jump to main
   jmp main
