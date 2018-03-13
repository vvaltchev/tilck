
.intel_syntax noprefix

.section .text

.macro irq number
   .global irq\number
   .type irq\number, @function
   irq\number:
   push 0
   push 32+\number
   jmp asm_int_handler
   .size irq\number, .-irq\number
.endm

irq 0
irq 1
irq 2
irq 3
irq 4
irq 5
irq 6
irq 7
irq 8
irq 9
irq 10
irq 11
irq 12
irq 13
irq 14
irq 15
