[BITS 32]

extern kmain
extern idtp
extern irq_handler
extern gdt_pointer
extern generic_interrupt_handler
extern disable_preemption
extern save_current_task_state
extern schedule_outside_interrupt_context

global gdt_load
global idt_load
global tss_flush
global _start
global asm_context_switch_x86
global asm_kernel_context_switch_x86
global kernel_yield
global panic_save_current_state

section .text

_start:

   ; before jump to kernel, we have to setup a basic paging
   ; in order to map the kernel from 0x100000 to 0xC0000000 (+3 GB)

   ; let's put the page directory at 0x1000 (+ 4 KB)

   mov edi, 0x1000

   .l1:
   mov dword [edi], 0
   add edi, 4
   cmp edi, 0x2000
   jne .l1

   ; let's put the first page table at 0x2000 (+ 8 KB)
   mov eax, 0
   .l2:

   mov ebx, eax
   or ebx, 3  ; present, rw
   mov [edi], ebx

   add eax, 0x1000 ; += 4K
   add edi, 4

   cmp edi, 0x3000
   jne .l2

   ; xchg bx, bx ; bochs magic break

   mov eax, 0x2003    ; = 0x2000 | preset,rw

   ; identity map the first low 4 MB
   ; this is necessary for executing the 'jmp eax' below)
   ; otherwise, just after 'mov cr0, eax', EIP will point to an invalid address

   mov [0x1000], eax
   mov [0x1C00], eax  ; map them also to 0xC0000000

   mov ebx, 0x1000
   mov cr3, ebx     ; set page dir physical address

   mov eax, cr0
   or eax, 0x80000000 ; paging ON
   or eax, 0x10000    ; WP ON (write protect for supervisor)
   mov cr0, eax       ; enable paging!

   mov eax, cr4
   or eax, 16      ; 16 = bit 4. PSE (Page Size Extension)
                   ; We enable it just in case we decide to use 4-MB pages
                   ; for the kernel.
   or eax, 128     ; 128 = bit 7. PGE (Page Global Enabled)
   mov cr4, eax

   mov eax, .next_step
   jmp eax        ; Jump to next instruction using the high virtual address.

                  ; This is necessary since here the EIP is still a physical
                  ; address, while in the kernel the physical identity mapping
                  ; is removed. We need to continue using high (+3 GB)
                  ; virtual addresses. The trick works because this file is
                  ; part of the original kernel ELF binary where the ORG
                  ; is set to +3 GB.

.next_step:
   mov esp, 0xC000FFF0 ; +64 KB - 16
   jmp kmain   ; now, really jump to kernel's code which uses 0xC0100000 as ORG

gdt_load:
   lgdt [gdt_pointer]
   ret

idt_load:
    lidt [idtp]
    ret

tss_flush:
   mov ax, 0x2B      ; Load the index of our TSS structure - The index is
                     ; 0x28, as it is the 5th selector and each is 8 bytes
                     ; long, but we set the bottom two bits (making 0x2B)
                     ; so that it has an RPL of 3, not zero.
   ltr ax            ; Load 0x2B into the task state register.
   ret


asm_context_switch_x86:

   pop eax ; return addr (ignored)

   pop eax ; gs
   mov gs, ax

   pop eax ; fs
   mov fs, ax

   pop eax ; es
   mov es, ax

   pop eax ; ds
   mov ds, ax

   ; We can't do 'popa' here because it will restore ESP
   pop edi
   pop esi
   pop ebp
   pop ebx
   pop edx
   pop ecx
   pop eax

   ; eip, cs, eflags, useresp and ss
   ; are already on stack, passed by the caller.

   ; debug ;;;;;;;;;;;;;;;;;;;
   ;pop eax ; entry point (eip)
   ;pop eax ; 0x1b (cs)
   ;pop eax ; eflags
   ;pop eax ; stack
   ;pop eax ; 0x23 (ss)
   ;;;;;;;;;;;;;;;;;;;;;;;;;;;

   iret


asm_kernel_context_switch_x86:

   pop eax ; return addr (ignored)

   pop eax ; eip
   pop ebx ; new esp

   ; we have to simulate a push of the eip, but using the stack in EBX in order
   ; to allow the final RET.

   mov [ebx], eax
   sub ebx, 4

   pop eax ; gs
   mov gs, ax

   pop eax ; fs
   mov fs, ax

   pop eax ; es
   mov es, ax

   pop eax ; ds
   mov ds, ax

   ; We can't do 'popa' here because it will restore ESP
   pop edi
   pop esi
   pop ebp
   pop ebx
   pop edx
   pop ecx
   pop eax

   popf ; pop the eflags register

   ; Now we can finally pop the 2nd copy of the ESP and just do RET since
   ; the right EIP is already on the new stack.
   pop esp

   sti ; This is mandatory here, after setting ESP since the eflags register
       ; for kernel context switches needs to have interrupts disabled.
       ; That's in order to allow the above 'pop esp' to happen, otherwise,
       ; we won't be able to ASSERT that the ESP's page is the same as
       ; current->kernel_stack.

   ret



; This is our common ISR stub. It saves the processor state, sets
; up for kernel mode segments, calls the C-level fault handler,
; and finally restores the stack frame.
asm_int_handler:

   pusha          ;  Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax
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
   mov eax, generic_interrupt_handler
   call eax
   pop eax
   pop gs
   pop fs
   pop es
   pop ds
   popa          ; Pops edi,esi,ebp...
   add esp, 8    ; Cleans up the pushed error code and pushed ISR number
   iret


; Saves the current (kernel) state as if an interrupt occurred while running
; in kernel mode.

kernel_yield:

   call disable_preemption
   cli

   pop eax   ; pop eip (return addr)
   push eax  ; re-push it back to the stack, as if the POP above never happened

   push cs
   push eax  ; eip (we saved before)
   push 0    ; err_code
   push -1   ; int_num

   pusha     ; Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax
   push ds
   push es
   push fs
   push gs

   mov eax, esp
   push eax
   mov eax, save_current_task_state
   call eax

   sti
   call schedule_outside_interrupt_context

panic_save_current_state:

   pop eax   ; pop eip (return addr)
   push eax  ; re-push it back to the stack, as if the POP above never happened

   push cs
   push eax  ; eip (we saved before)
   push 0    ; err_code
   push -1   ; int_num

   pusha     ; Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax
   push ds
   push es
   push fs
   push gs

   mov eax, esp
   push eax
   mov eax, save_current_task_state
   call eax

   add esp, 68
   ret

; Service Routines (ISRs)
global isr0
global isr1
global isr2
global isr3
global isr4
global isr5
global isr6
global isr7
global isr8
global isr9
global isr10
global isr11
global isr12
global isr13
global isr14
global isr15
global isr16
global isr17
global isr18
global isr19
global isr20
global isr21
global isr22
global isr23
global isr24
global isr25
global isr26
global isr27
global isr28
global isr29
global isr30
global isr31

global isr128

;  0: Divide By Zero Exception
isr0:
   cli
   push 0
   push 0
   jmp asm_int_handler

;  1: Debug Exception
isr1:
   cli
   push 0
   push 1
   jmp asm_int_handler

;  2: Non Maskable Interrupt Exception
isr2:
   cli
   push 0
   push 2
   jmp asm_int_handler

;  3: Int 3 Exception
isr3:
   cli
   push 0
   push 3
   jmp asm_int_handler

;  4: INTO Exception
isr4:
   cli
   push 0
   push 4
   jmp asm_int_handler

;  5: Out of Bounds Exception
isr5:
   cli
   push 0
   push 5
   jmp asm_int_handler

;  6: Invalid Opcode Exception
isr6:
   cli
   push 0
   push 6
   jmp asm_int_handler

;  7: Coprocessor Not Available Exception
isr7:
   cli
   push 0
   push 7
   jmp asm_int_handler

;  8: Double Fault Exception (With Error Code!)
isr8:
   cli
   push 8
   jmp asm_int_handler

;  9: Coprocessor Segment Overrun Exception
isr9:
   cli
   push 0
   push 9
   jmp asm_int_handler

; 10: Bad TSS Exception (With Error Code!)
isr10:
   cli
   push 10
   jmp asm_int_handler

; 11: Segment Not Present Exception (With Error Code!)
isr11:
   cli
   push 11
   jmp asm_int_handler

; 12: Stack Fault Exception (With Error Code!)
isr12:
   cli
   push 12
   jmp asm_int_handler

; 13: General Protection Fault Exception (With Error Code!)
isr13:
   cli
   push 13
   jmp asm_int_handler

; 14: Page Fault Exception (With Error Code!)
isr14:
   cli
   push 14
   jmp asm_int_handler

; 15: Reserved Exception
isr15:
   cli
   push 0
   push 15
   jmp asm_int_handler

; 16: Floating Point Exception
isr16:
   cli
   push 0
   push 16
   jmp asm_int_handler

; 17: Alignment Check Exception
isr17:
   cli
   push 0
   push 17
   jmp asm_int_handler

; 18: Machine Check Exception
isr18:
   cli
   push 0
   push 18
   jmp asm_int_handler

; 19: Reserved
isr19:
   cli
   push 0
   push 19
   jmp asm_int_handler

; 20: Reserved
isr20:
   cli
   push 0
   push 20
   jmp asm_int_handler

; 21: Reserved
isr21:
   cli
   push 0
   push 21
   jmp asm_int_handler

; 22: Reserved
isr22:
   cli
   push 0
   push 22
   jmp asm_int_handler

; 23: Reserved
isr23:
   cli
   push 0
   push 23
   jmp asm_int_handler

; 24: Reserved
isr24:
   cli
   push 0
   push 24
   jmp asm_int_handler

; 25: Reserved
isr25:
   cli
   push 0
   push 25
   jmp asm_int_handler

; 26: Reserved
isr26:
   cli
   push 0
   push 26
   jmp asm_int_handler

; 27: Reserved
isr27:
   cli
   push 0
   push 27
   jmp asm_int_handler

; 28: Reserved
isr28:
   cli
   push 0
   push 28
   jmp asm_int_handler

; 29: Reserved
isr29:
   cli
   push 0
   push 29
   jmp asm_int_handler

; 30: Reserved
isr30:
   cli
   push 0
   push 30
   jmp asm_int_handler

; 31: Reserved
isr31:
   cli
   push 0
   push 31
   jmp asm_int_handler

; int 0x80: syscall
isr128:
   cli
   push 0
   push 0x80
   jmp asm_int_handler


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

global irq0
global irq1
global irq2
global irq3
global irq4
global irq5
global irq6
global irq7
global irq8
global irq9
global irq10
global irq11
global irq12
global irq13
global irq14
global irq15

; 32: IRQ0
irq0:
   cli
   push 0
   push 32
   jmp asm_int_handler

; 33: IRQ1
irq1:
   cli
   push 0
   push 33
   jmp asm_int_handler

; 34: IRQ2
irq2:
   cli
   push 0
   push 34
   jmp asm_int_handler

; 35: IRQ3
irq3:
   cli
   push 0
   push 35
   jmp asm_int_handler

; 36: IRQ4
irq4:
   cli
   push 0
   push 36
   jmp asm_int_handler

; 37: IRQ5
irq5:
   cli
   push 0
   push 37
   jmp asm_int_handler

; 38: IRQ6
irq6:
   cli
   push 0
   push 38
   jmp asm_int_handler

; 39: IRQ7
irq7:
   cli
   push 0
   push 39
   jmp asm_int_handler

; 40: IRQ8
irq8:
   cli
   push 0
   push 40
   jmp asm_int_handler

; 41: IRQ9
irq9:
   cli
   push 0
   push 41
   jmp asm_int_handler

; 42: IRQ10
irq10:
   cli
   push 0
   push 42
   jmp asm_int_handler

; 43: IRQ11
irq11:
   cli
   push 0
   push 43
   jmp asm_int_handler

; 44: IRQ12
irq12:
   cli
   push 0
   push 44
   jmp asm_int_handler

; 45: IRQ13
irq13:
   cli
   push 0
   push 45
   jmp asm_int_handler

; 46: IRQ14
irq14:
   cli
   push 0
   push 46
   jmp asm_int_handler

; 47: IRQ15
irq15:
   cli
   push 0
   push 47
   jmp asm_int_handler

