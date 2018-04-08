
.intel_syntax noprefix

#define ASM_FILE 1
#include <common/config.h>

.section .text
.global irq_entry_points

.macro create_irq_entry_point number
   FUNC(irq\number):
   push 0
   push 32+\number
   jmp asm_int_handler
   END_FUNC(irq\number)
.endm

.altmacro

.set i, 0
.rept 16
   create_irq_entry_point %i
   .set i, i+1
.endr

.macro insert_irq_addr num
   .long irq\num
.endm

irq_entry_points:
.set i, 0
.rept 16
   insert_irq_addr %i
   .set i, i+1
.endr
