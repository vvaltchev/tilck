[BITS 32]

extern _init_main

section .text

jmp _init_main

times 16384 dd 0xAABBCC00 ; 64 KB
times 16384 dd 0xAABBCC01 ; 64 KB
times 16384 dd 0xAABBCC02 ; 64 KB
times 16384 dd 0xAABBCC03 ; 64 KB
times 16384 dd 0xAABBCC04 ; 64 KB
times 16384 dd 0xAABBCC05 ; 64 KB

