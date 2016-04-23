
;times 4096 dd 0xAABBCCDD

extern _init_main

section .text

jmp _init_main