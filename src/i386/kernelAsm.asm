[BITS 32]

extern kmain
extern idtp
extern irq_handler
extern gdt_pointer

global gdt_load
global idt_load
global tss_flush
global _start

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
   ; this is necessary for executing the jmp far eax below)
   ; otherwise, just after 'mov cr0, eax', EIP will point to an invalid address
   
   mov [0x1000], eax
   mov [0x1C00], eax  ; map them also to 0xC0000000
   
   mov ebx, 0x1000
   mov cr3, ebx     ; set page dir physical address
   
   mov eax, cr0
   or eax, 0x80000000 ; paging ON
   or eax, 0x10000    ; WP ON (write protect for supervisor)
   mov cr0, eax       ; enable paging!
  
   mov eax, 0xC0100400
   jmp far eax        ; jump to next instruction using the high virtual address

   times 1024-($-$$) db 0
   
   ; this is 0xC0100400
   mov esp, 0xC01FFFF0
   jmp kmain        ; now, really jump to kernel's code which uses 0xC0100000 as ORG
   
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

global switch_to_usermode_asm
   
switch_to_usermode_asm:
  mov ax,0x23 ; user data selector
  mov ds,ax
  mov es,ax 
  mov fs,ax 
  mov gs,ax ; we don't need to worry about SS. it's handled by iret
  
  mov ebx, [esp + 4]  ; first arg, the usermode entry point
  mov eax, [esp + 8]  ; second arg, the usermode stack ptr
  
  push 0x23 ; user data selector with bottom 2 bits set for ring 3
  push eax  ; push the stack pointer
  pushf     ; push the EFLAGS register onto the stack
  push 0x1B ; user code selector with bottom 2 bits set for ring 3
  push ebx  
  iret
   
global asm_context_switch_x86

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
      
   pop edi
   pop esi
   pop ebp
   pop ebx
   pop edx
   pop ecx
   pop eax
      
   ; debug ;;;;;;;;;;;;;;;;;;;
   ;pop eax ; entry point (eip)
   ;pop eax ; 0x1b (cs)
   ;pop eax ; eflags
   ;pop eax ; stack
   ;pop eax ; 0x23 (ds)
   ;;;;;;;;;;;;;;;;;;;;;;;;;;;
   
   iret
   

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
    ; cli
    push byte 0
    push byte 0
    jmp isr_common_stub

;  1: Debug Exception
isr1:
    ; cli
    push byte 0
    push byte 1
    jmp isr_common_stub

;  2: Non Maskable Interrupt Exception
isr2:
    ; cli
    push byte 0
    push byte 2
    jmp isr_common_stub

;  3: Int 3 Exception
isr3:
    ; cli
    push byte 0
    push byte 3
    jmp isr_common_stub

;  4: INTO Exception
isr4:
    ; cli
    push byte 0
    push byte 4
    jmp isr_common_stub

;  5: Out of Bounds Exception
isr5:
    ; cli
    push byte 0
    push byte 5
    jmp isr_common_stub

;  6: Invalid Opcode Exception
isr6:
    ; cli
    push byte 0
    push byte 6
    jmp isr_common_stub

;  7: Coprocessor Not Available Exception
isr7:
    ; cli
    push byte 0
    push byte 7
    jmp isr_common_stub

;  8: Double Fault Exception (With Error Code!)
isr8:
    ; cli
    push byte 8
    jmp isr_common_stub

;  9: Coprocessor Segment Overrun Exception
isr9:
    ; cli
    push byte 0
    push byte 9
    jmp isr_common_stub

; 10: Bad TSS Exception (With Error Code!)
isr10:
    ; cli
    push byte 10
    jmp isr_common_stub

; 11: Segment Not Present Exception (With Error Code!)
isr11:
    ; cli
    push byte 11
    jmp isr_common_stub

; 12: Stack Fault Exception (With Error Code!)
isr12:
    ; cli
    push byte 12
    jmp isr_common_stub

; 13: General Protection Fault Exception (With Error Code!)
isr13:
    ; cli
    push byte 13
    jmp isr_common_stub

; 14: Page Fault Exception (With Error Code!)
isr14:
    ; cli
    push byte 14
    jmp isr_common_stub

; 15: Reserved Exception
isr15:
    ; cli
    push byte 0
    push byte 15
    jmp isr_common_stub

; 16: Floating Point Exception
isr16:
    ; cli
    push byte 0
    push byte 16
    jmp isr_common_stub

; 17: Alignment Check Exception
isr17:
    ; cli
    push byte 0
    push byte 17
    jmp isr_common_stub

; 18: Machine Check Exception
isr18:
    ; cli
    push byte 0
    push byte 18
    jmp isr_common_stub

; 19: Reserved
isr19:
    ; cli
    push byte 0
    push byte 19
    jmp isr_common_stub

; 20: Reserved
isr20:
    ; cli
    push byte 0
    push byte 20
    jmp isr_common_stub

; 21: Reserved
isr21:
    ; cli
    push byte 0
    push byte 21
    jmp isr_common_stub

; 22: Reserved
isr22:
    ; cli
    push byte 0
    push byte 22
    jmp isr_common_stub

; 23: Reserved
isr23:
    ; cli
    push byte 0
    push byte 23
    jmp isr_common_stub

; 24: Reserved
isr24:
    ; cli
    push byte 0
    push byte 24
    jmp isr_common_stub

; 25: Reserved
isr25:
    ; cli
    push byte 0
    push byte 25
    jmp isr_common_stub

; 26: Reserved
isr26:
    ; cli
    push byte 0
    push byte 26
    jmp isr_common_stub

; 27: Reserved
isr27:
    ; cli
    push byte 0
    push byte 27
    jmp isr_common_stub

; 28: Reserved
isr28:
    ; cli
    push byte 0
    push byte 28
    jmp isr_common_stub

; 29: Reserved
isr29:
    ; cli
    push byte 0
    push byte 29
    jmp isr_common_stub

; 30: Reserved
isr30:
    ; cli
    push byte 0
    push byte 30
    jmp isr_common_stub

; 31: Reserved
isr31:
    ; cli
    push byte 0
    push byte 31
    jmp isr_common_stub

isr128:
    push byte 0
    push 0x80
    jmp isr_common_stub


extern generic_interrupt_handler

; This is our common ISR stub. It saves the processor state, sets
; up for kernel mode segments, calls the C-level fault handler,
; and finally restores the stack frame.
isr_common_stub:
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
    ; cli
    push byte 0
    push byte 32
    jmp irq_common_stub

; 33: IRQ1
irq1:
    ; cli
    push byte 0
    push byte 33
    jmp irq_common_stub

; 34: IRQ2
irq2:
    ; cli
    push byte 0
    push byte 34
    jmp irq_common_stub

; 35: IRQ3
irq3:
    ; cli
    push byte 0
    push byte 35
    jmp irq_common_stub

; 36: IRQ4
irq4:
    ; cli
    push byte 0
    push byte 36
    jmp irq_common_stub

; 37: IRQ5
irq5:
    ; cli
    push byte 0
    push byte 37
    jmp irq_common_stub

; 38: IRQ6
irq6:
    ; cli
    push byte 0
    push byte 38
    jmp irq_common_stub

; 39: IRQ7
irq7:
    ; cli
    push byte 0
    push byte 39
    jmp irq_common_stub

; 40: IRQ8
irq8:
    ; cli
    push byte 0
    push byte 40
    jmp irq_common_stub

; 41: IRQ9
irq9:
    ; cli
    push byte 0
    push byte 41
    jmp irq_common_stub

; 42: IRQ10
irq10:
    ; cli
    push byte 0
    push byte 42
    jmp irq_common_stub

; 43: IRQ11
irq11:
    ; cli
    push byte 0
    push byte 43
    jmp irq_common_stub

; 44: IRQ12
irq12:
    ; cli
    push byte 0
    push byte 44
    jmp irq_common_stub

; 45: IRQ13
irq13:
    ; cli
    push byte 0
    push byte 45
    jmp irq_common_stub

; 46: IRQ14
irq14:
    ; cli
    push byte 0
    push byte 46
    jmp irq_common_stub

; 47: IRQ15
irq15:
    ; cli
    push byte 0
    push byte 47
    jmp irq_common_stub



irq_common_stub:
    pusha
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
    mov eax, irq_handler
    call eax
    pop eax

    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    iret

