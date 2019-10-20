# SPDX-License-Identifier: BSD-2-Clause

.intel_syntax noprefix

#define ASM_FILE 1

#include <tilck/common/config.h>
#include <tilck/kernel/arch/i386/asm_defs.h>
#include <multiboot.h>

.section .text

.code32

.global asm_kernel_yield
.global panic_save_current_state
.global switch_to_initial_kernel_stack

# Saves the current (kernel) state as if an interrupt occurred while running
# in kernel mode.

FUNC(asm_kernel_yield):

   lock inc dword ptr [disable_preemption_count]

   pushf             # push EFLAGS
   sub esp, 16       # skip cs, eip, err_code, int_num

   save_base_regs
   skip_push_custom_flags

   push offset .kernel_yield_resume
   mov eax, esp

   push eax
   call save_current_task_state

#if defined(DEBUG) && KRN_TRACK_NESTED_INTERR
   call check_not_in_irq_handler
#endif

   call schedule_outside_interrupt_context

   # schedule() just returned: restore ESP and just return

   add esp, SIZEOF_REGS - 8 + 4 # -8 : we skipped ss, useresp
                                # +4 : compensate the last push eax above

   lock dec dword ptr [disable_preemption_count]
   ret

.kernel_yield_resume:

   skip_pop_custom_flags
   resume_base_regs
   add esp, 16     # Discard int_num, err_code, eip, cs
   popf
   ret

END_FUNC(asm_kernel_yield)

FUNC(panic_save_current_state):

   push ss
   push 0xcafebabe   # placeholder for useresp
   pushf             # push EFLAGS
   push cs
   push 0xcafecafe   # placeholder for eip

   push 0            # err_code
   push -1           # int_num

   save_base_regs
   skip_push_custom_flags

   push offset .kernel_yield_resume
   mov eax, esp

   mov ecx, [esp + SIZEOF_REGS]             # ecx = caller's EIP
   mov [eax + REGS_EIP_OFF], ecx            # struct regs->eip = ecx

   lea ecx, [esp + SIZEOF_REGS]
   mov [eax + REGS_USERESP_OFF], ecx        # struct regs->useresp = ecx

   push eax
   call panic_save_current_task_state
   add esp, SIZEOF_REGS + 4                 # +4 because the last push eax here
   ret

END_FUNC(panic_save_current_state)

FUNC(switch_to_initial_kernel_stack):

   # save the return addr in eax
   pop eax

   # save in ecx the ebp-esp offset
   mov ecx, ebp
   sub ecx, esp

   # change the stack pointer and the frame pointer
   mov ebp, offset kernel_initial_stack + KERNEL_STACK_SIZE - 4
   mov esp, ebp

   # restore the ebp-esp difference
   sub esp, ecx

   # jump back to the return addr. Faster than: push eax + ret.
   jmp eax

END_FUNC(switch_to_initial_kernel_stack)

