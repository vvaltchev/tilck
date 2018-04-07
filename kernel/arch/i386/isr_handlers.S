
.intel_syntax noprefix

#define ASM_FILE 1
#include <common/config.h>

.section .text


.macro isr number
   .global isr\number
   FUNC(isr\number):
   push 0
   push \number
   jmp asm_int_handler
   END_FUNC(isr\number)
.endm

.macro isr_with_err_code number
   .global isr\number
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

.global ex_entry_points_array

ex_entry_points_array:
   .long isr0
   .long isr1
   .long isr2
   .long isr3
   .long isr4
   .long isr5
   .long isr6
   .long isr7
   .long isr8
   .long isr9
   .long isr10
   .long isr11
   .long isr12
   .long isr13
   .long isr14
   .long isr15
   .long isr16
   .long isr17
   .long isr18
   .long isr19
   .long isr20
   .long isr21
   .long isr22
   .long isr23
   .long isr24
   .long isr25
   .long isr26
   .long isr27
   .long isr28
   .long isr28
   .long isr29
   .long isr30
   .long isr31


