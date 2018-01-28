[BITS 32]

extern kmain
extern gdt_pointer
extern generic_interrupt_handler
extern disable_preemption
extern save_current_task_state
extern schedule_outside_interrupt_context

global _start
global asm_context_switch_x86
global asm_kernel_context_switch_x86
global kernel_yield
global panic_save_current_state

global asm_int_handler

section .text




;;;;;;;;;;;;;;;;;;;;;;;;
