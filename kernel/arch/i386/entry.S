
.intel_syntax noprefix

#include <config.h>

.section .text

.global _start
.global gdt_load
.global idt_load
.global tss_flush
.global asm_int_handler
.global asm_context_switch_x86
.global asm_kernel_context_switch_x86
.global kernel_yield
.global panic_save_current_state

_start:

   # before jump to kernel, we have to setup a basic paging
   # in order to map the kernel from 0x100000 to 0xC0000000 (+3 GB)

   # let's put the page directory at 0x1000 (+ 4 KB)

   mov edi, 0x1000

   .l1:
   mov dword ptr [edi], 0
   add edi, 4
   cmp edi, 0x2000
   jne .l1

   # let's put the first page table at 0x2000 (+ 8 KB)
   mov eax, 0
   .l2:

   mov ebx, eax
   or ebx, 3  # present, rw
   mov [edi], ebx

   add eax, 0x1000 # += 4K
   add edi, 4

   cmp edi, 0x3000
   jne .l2

   # xchg bx, bx # bochs magic break

   mov eax, 0x2003    # = 0x2000 | preset,rw

   # identity map the first low 4 MB
   # this is necessary for executing the 'jmp eax' below)
   # otherwise, just after 'mov cr0, eax', EIP will point to an invalid address

   mov [0x1000], eax
   mov [0x1C00], eax  # map them also to 0xC0000000

   mov ebx, 0x1000
   mov cr3, ebx     # set page dir physical address

   mov eax, cr0
   or eax, 0x80000000 # paging ON
   or eax, 0x10000    # WP ON (write protect for supervisor)
   mov cr0, eax       # enable paging!

   mov eax, cr4
   or eax, 16      # 16 = bit 4. PSE (Page Size Extension)
                   # We enable it just in case we decide to use 4-MB pages
                   # for the kernel.
   or eax, 128     # 128 = bit 7. PGE (Page Global Enabled)
   mov cr4, eax

   mov eax, offset .next_step
   jmp eax        # Jump to next instruction using the high virtual address.

                  # This is necessary since here the EIP is still a physical
                  # address, while in the kernel the physical identity mapping
                  # is removed. We need to continue using high (+3 GB)
                  # virtual addresses. The trick works because this file is
                  # part of the kernel ELF binary where the ORGvis set to
                  # 0xC0100000.

.next_step:
   mov esp, KERNEL_INITIAL_STACK_ADDR
   jmp kmain   # now, really jump to kernel's code which uses 0xC0100000 as ORG


gdt_load:
   lgdt [gdt_pointer]
   ret

idt_load:
    lidt [idtp]
    ret

tss_flush:
   mov ax, 0x2B      # Load the index of our TSS structure - The index is
                     # 0x28, as it is the 5th selector and each is 8 bytes
                     # long, but we set the bottom two bits (making 0x2B)
                     # so that it has an RPL of 3, not zero.
   ltr ax            # Load 0x2B into the task state register.
   ret


.global isr128

# int 0x80: syscall
isr128:
   push 0
   push 0x80
   jmp asm_int_handler

# This is our common ISR stub. It saves the processor state, sets
# up for kernel mode segments, calls the C-level fault handler,
# and finally restores the stack frame.
asm_int_handler:

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
   mov eax, esp
   push eax
   mov eax, offset generic_interrupt_handler
   call eax
   pop eax
   pop gs
   pop fs
   pop es
   pop ds
   popa          # Pops edi,esi,ebp...
   add esp, 8    # Cleans up the pushed error code and pushed ISR number
   iret


asm_context_switch_x86:

   pop eax # return addr (ignored)

   pop eax # gs
   mov gs, ax

   pop eax # fs
   mov fs, ax

   pop eax # es
   mov es, ax

   pop eax # ds
   mov ds, ax

   # We can't do 'popa' here because it will restore ESP
   pop edi
   pop esi
   pop ebp
   pop ebx
   pop edx
   pop ecx
   pop eax

   # eip, cs, eflags, useresp and ss
   # are already on stack, passed by the caller.

   # debug ###################
   #pop eax # entry point (eip)
   #pop eax # 0x1b (cs)
   #pop eax # eflags
   #pop eax # stack
   #pop eax # 0x23 (ss)
   ###########################

   iret


asm_kernel_context_switch_x86:

   pop eax # return addr (ignored)

   pop eax # eip
   pop ebx # new esp

   # we have to simulate a push of the eip, but using the stack in EBX in order
   # to allow the final RET.

   mov [ebx], eax
   sub ebx, 4

   pop eax # gs
   mov gs, ax

   pop eax # fs
   mov fs, ax

   pop eax # es
   mov es, ax

   pop eax # ds
   mov ds, ax

   # We can't do 'popa' here because it will restore ESP
   pop edi
   pop esi
   pop ebp
   pop ebx
   pop edx
   pop ecx
   pop eax

   popf # pop the eflags register

   # Now we can finally pop the 2nd copy of the ESP and just do RET since
   # the right EIP is already on the new stack.
   pop esp

   sti # This is mandatory here, after setting ESP since the eflags register
       # for kernel context switches needs to have interrupts disabled.
       # That's in order to allow the above 'pop esp' to happen, otherwise,
       # we won't be able to ASSERT that the ESP's page is the same as
       # current->kernel_stack.

   ret


# Saves the current (kernel) state as if an interrupt occurred while running
# in kernel mode.

kernel_yield:

   call disable_preemption
   cli

   pop eax   # pop eip (return addr)
   push eax  # re-push it back to the stack, as if the POP above never happened

   push cs
   push eax  # eip (we saved before)
   push 0    # err_code
   push -1   # int_num

   pusha     # Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax
   push ds
   push es
   push fs
   push gs

   mov eax, esp
   push eax
   mov eax, offset save_current_task_state
   call eax

   sti
   call schedule_outside_interrupt_context

panic_save_current_state:

   pop eax   # pop eip (return addr)
   push eax  # re-push it back to the stack, as if the POP above never happened

   push cs
   push eax  # eip (we saved before)
   push 0    # err_code
   push -1   # int_num

   pusha     # Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax
   push ds
   push es
   push fs
   push gs

   mov eax, esp
   push eax
   mov eax, offset save_current_task_state
   call eax

   add esp, 68
   ret
