
.intel_syntax noprefix

#define ASM_FILE 1
#include <common/config.h>

.section .text
.global irq_entry_points
.global asm_irq_entry

# IRQs common entry point
FUNC(asm_irq_entry):

   pusha          #  Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax
   push ds
   push es
   push fs
   push gs
   mov ax, 0x10
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax

   push offset .irq_resume

   mov eax, esp
   push eax
   cld            # Set DF = 0, as C compilers by default assume that.
   call irq_entry

   add esp, 4     # Discard the previousy-pushed 'eax'
   add esp, 4     # skip

.irq_resume:
   pop gs
   pop fs
   pop es
   pop ds
   popa
   add esp, 8     # Discards the pushed err_code and int_num
   iret

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

irq_entry_points:
.set i, 0
.rept 16
   insert_irq_addr %i
   .set i, i+1
.endr
