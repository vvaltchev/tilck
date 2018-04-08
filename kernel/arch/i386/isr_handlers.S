
.intel_syntax noprefix

#define ASM_FILE 1
#include <common/config.h>

.section .text
.global isr_entry_points


.macro isr number
   FUNC(isr\number):
   push 0
   push \number
   jmp asm_int_handler
   END_FUNC(isr\number)
.endm

.macro isr_with_err_code number
   FUNC(isr\number):
   push \number
   jmp asm_int_handler
   END_FUNC(isr\number)
.endm

# Service Routines (ISRs)

isr 0 # Divide By Zero Exception
isr 1 # Debug Exception
isr 2 # Non Maskable Interrupt Exception
isr 3 # Int 3 Exception
isr 4 # INTO Exception
isr 5 # Out of Bounds Exception
isr 6 # Invalid Opcode Exception
isr 7 # Coprocessor Not Available Exception

isr_with_err_code 8 #  8: Double Fault Exception

isr 9 # Coprocessor Segment Overrun Exception

isr_with_err_code 10 # 10: Bad TSS Exception
isr_with_err_code 11 # Segment Not Present Exception
isr_with_err_code 12 # Stack Fault Exception
isr_with_err_code 13 # General Protection Fault Exception
isr_with_err_code 14 # Page Fault Exception

isr 15 # Reserved Exception
isr 16 # Floating Point Exception
isr 17 # Alignment Check Exception
isr 18 # Machine Check Exception

# Reserved ISRs

isr 19
isr 20
isr 21
isr 22
isr 23
isr 24
isr 25
isr 26
isr 27
isr 28
isr 29
isr 30
isr 31


.altmacro

.macro insert_isr_addr num
   .long isr\num
.endm

isr_entry_points:
.set i, 0
.rept 32
   insert_isr_addr %i
   .set i, i+1
.endr
