

extern _init_main

section .text

jmp _init_main

times 32768 dd 0xAABBCCDD
