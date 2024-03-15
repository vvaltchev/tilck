# SPDX-License-Identifier: BSD-2-Clause

.intel_syntax noprefix

#define ASM_FILE 1
#include <tilck_gen_headers/config_global.h>
#include <tilck/kernel/arch/i386/asm_defs.h>

.section .text
.global fault_entry_points
.global double_fault_handler_asm

.macro fault number
   FUNC(fault\number):
   push 0
   push \number
   jmp asm_fault_entry
   END_FUNC(fault\number)
.endm

.macro fault_with_err_code number
   FUNC(fault\number):
   push \number
   jmp asm_fault_entry
   END_FUNC(fault\number)
.endm

fault 0 # Divide By Zero Exception
fault 1 # Debug Exception
fault 2 # Non Maskable Interrupt Exception
fault 3 # Int 3 Exception
fault 4 # INTO Exception
fault 5 # Out of Bounds Exception
fault 6 # Invalid Opcode Exception
fault 7 # Coprocessor Not Available Exception

fault_with_err_code 8 # Double Fault Exception

fault 9 # Coprocessor Segment Overrun Exception

fault_with_err_code 10 # Bad TSS Exception
fault_with_err_code 11 # Segment Not Present Exception
fault_with_err_code 12 # Stack Fault Exception
fault_with_err_code 13 # General Protection Fault Exception
fault_with_err_code 14 # Page Fault Exception

fault 15 # Reserved Exception
fault 16 # Floating Point Exception
fault 17 # Alignment Check Exception
fault 18 # Machine Check Exception

fault 19
fault 20
fault 21
fault 22
fault 23
fault 24
fault 25
fault 26
fault 27
fault 28
fault 29
fault 30
fault 31


.altmacro

.macro insert_fault_addr num
   .long fault\num
.endm

.align 4
fault_entry_points:
.set i, 0
.rept 32
   insert_fault_addr %i
   .set i, i+1
.endr

FUNC(double_fault_handler_asm):

   cld                      # Set DF = 0, as C compilers by default assume that.
   jmp double_fault_handler # Direct jmp, no call: the callee won't return

END_FUNC(double_fault_handler_asm)


FUNC(asm_fault_entry):

   kernel_entry_common
   push_custom_flags (0)

   push offset fault_entry_resume
   mov eax, esp
   cld            # Set DF = 0, as C compilers by default assume that.
   push eax
   call fault_entry

   add esp, 8     # Discard the previousy-pushed 'eax' and kernel_resume_eip

# NOTE: this must be identical to `soft_interrupt_resume` defined in
# syscall_entry.S. Here it's simply copy-pasted to save an extra jump.
fault_entry_resume:
   pop_custom_flags
   kernel_exit_common

END_FUNC(asm_fault_entry)

# Tell GNU ld to not worry about us having an executable stack
.section .note.GNU-stack,"",@progbits
