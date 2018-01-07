
%define KERNEL_PADDR  0x00100000 ; +   1 MB

extern main

global _start
global jump_to_kernel

_start:

   jmp main

jump_to_kernel:
   jmp 0x08:KERNEL_PADDR
