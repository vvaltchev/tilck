# SPDX-License-Identifier: BSD-2-Clause

.intel_syntax noprefix

#define ASM_FILE 1
#include <tilck_gen_headers/config_global.h>
#include <tilck/kernel/arch/i386/asm_defs.h>

.section .text
.global irq_entry_points
.global asm_irq_entry
.global irq_resume

# IRQs common entry point
FUNC(asm_irq_entry):

   kernel_entry_common
   push_custom_flags (0)

   push offset irq_resume
   mov eax, esp
   cld            # Set DF = 0, as C compilers by default assume that.
   push eax
   call irq_entry

   add esp, 8     # Discard the previousy-pushed 'eax' and .irq_resume

irq_resume:
   pop_custom_flags
   kernel_exit_common

END_FUNC(asm_irq_entry)

.macro create_irq_entry_point number
   FUNC(irq\number):
   push 0
   push 32+\number
   jmp asm_irq_entry
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

.align 4
irq_entry_points:
.set i, 0
.rept 16
   insert_irq_addr %i
   .set i, i+1
.endr

# Tell GNU ld to not worry about us having an executable stack
.section .note.GNU-stack,"",@progbits
