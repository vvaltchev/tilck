
[BITS 16]

   ; Our trivial bootloader has loaded this code at absolute address 0x20000
   ; now we have more than 512 bytes to execute

   jmp entry_point ; 3 bytes

gdt0 db 0, 0, 0, 0, 0, 0, 0, 0
gdt1 db 0xFF, 0xFF, 0, 0, 0, 0x9A, 0xCF, 0
gdt2 db 0xFF, 0xFF, 0, 0, 0, 0x92, 0xCF, 0
   
gdtr db 23,      0,  0, 0, 0, 0
idtr db 0, 0, 0, 0, 0, 0

helloStr db 'Hello, I am the 2nd stage-bootloader', 13, 10, 0      

 
%INCLUDE 'realModeStrings.inc'


   entry_point:
   
   cli             ; Clear interrupts
   mov ax, 0
   mov ss, ax      ; Set stack segment and pointer
   mov sp, 0FFFFh
   sti             ; Restore interrupts

   cld               ; The default direction for string operations
                     ; will be 'up' - incrementing address in RAM

   mov ax, 2000h     ; Set all segments to match where this code is loaded
   mov ds, ax        
   mov es, ax        
   mov fs, ax        
   mov gs, ax
 
   ; xchg bx, bx ; bochs magic break   

 
   push helloStr
   call print_string
   add sp, 2

   
   ; xchg bx, bx ; bochs magic break   
   
   cli          ; disable interrupts
  
   mov bx, gdt0
   
   ; the pointer of GDT must be an absolute 32bit address
   ; since the kernel is loaded in 0x2000:0x0000,
   ; this means, 0x20000 (note the EXTRA zero).
   ; the following code write this value (litte-endian)
   ; in to GDTR.
   
   mov word [gdtr+2], 0x03
   mov word [gdtr+3], 0x00
   mov word [gdtr+4], 0x02
   mov word [gdtr+5], 0x00

   
   ; xchg bx, bx ; bochs magic break    
   
   ; now we have to copy the text from
   ; complete_flush + 0x0 to complete_flush + 1 KB
   ; into 0x0000:0x1000
   
   mov si, complete_flush
   mov di, 0x1000
   mov cx, 512 ; 512 2-byte word
   
   mov ax, 0
   mov es, ax ; using extra segment for 0x0
   rep movsw ; copies 2*CX bytes from [di:si] to [es:di]
   
   call smart_enable_A20
  
   ;xchg bx, bx ; bochs magic break 

   lidt [idtr]
   
   
   ; layout of segments:
   ; http://stackoverflow.com/questions/23978486/far-jump-in-gdt-in-bootloader

   ; index (13b) | TI (2b) | RPL (2b)
   ; TI = table indicator; 0 = GDT, 1 = LDT
   ; RPL = Requestor priviledge level; 00 = highest, 11 = lowest
   
flush_gdt:
   lgdt [gdtr]  ; load GDT register with start address of Global Descriptor Table
   
   ; FIRST switch to protected mode and THEN do the FAR JUMP to 32 bit code
   
   mov eax, cr0 
   or al, 1     ; set PE (Protection Enable) bit in CR0 (Control Register 0)
   mov cr0, eax   
   
   ; xchg bx, bx ; bochs magic break 

   ; 0x08 is the first selector
   ; 0000000000001     0         00
   ; index 1 (code)   GDT    privileged
   
   jmp 0x08:0x1000 ; the JMP sets CS (code selector);


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Function: check_a20
;
; Purpose: to check the status of the a20 line in a completely self-contained state-preserving way.
;          The function can be modified as necessary by removing push's at the beginning and their
;          respective pop's at the end if complete self-containment is not required.
;
; Returns: 0 in ax if the a20 line is disabled (memory wraps around)
;          1 in ax if the a20 line is enabled (memory does not wrap around)
 
check_a20:
    pushf
    push ds
    push es
    push di
    push si
 
    cli
 
    xor ax, ax ; ax = 0
    mov es, ax
 
    not ax ; ax = 0xFFFF
    mov ds, ax
 
    mov di, 0x0500
    mov si, 0x0510
 
    mov al, byte [es:di]
    push ax
 
    mov al, byte [ds:si]
    push ax
 
    mov byte [es:di], 0x00
    mov byte [ds:si], 0xFF
 
    cmp byte [es:di], 0xFF
 
    pop ax
    mov byte [ds:si], al
 
    pop ax
    mov byte [es:di], al
 
    mov ax, 0
    je .check_a20__exit
 
    mov ax, 1
 
.check_a20__exit:
    pop si
    pop di
    pop es
    pop ds
    popf
 
    ret

enable_A20_bios:
   mov ax, 0x2401
   int 0x15
   ret
   
enable_A20_kb:
        cli
 
        call    .a20wait
        mov     al,0xAD
        out     0x64,al
 
        call    .a20wait
        mov     al,0xD0
        out     0x64,al
 
        call    .a20wait2
        in      al,0x60
        push    eax
 
        call    .a20wait
        mov     al,0xD1
        out     0x64,al
 
        call    .a20wait
        pop     eax
        or      al,2
        out     0x60,al
 
        call    .a20wait
        mov     al,0xAE
        out     0x64,al
 
        call    .a20wait
        sti
        ret
 
.a20wait:
        in      al,0x64
        test    al,2
        jnz     .a20wait
        ret
 
 
.a20wait2:
        in      al,0x64
        test    al,1
        jz      .a20wait2
        ret

enable_A20_fast_gate:
   in al, 0x92
   test al, 2
   jnz .after
   or al, 2
   and al, 0xFE
   out 0x92, al
   .after:
   ret

   
smart_enable_A20:

   call check_a20
   cmp ax, 0
   jne .end
   
   call enable_A20_bios

   call check_a20
   cmp ax, 0
   jne .end

   call enable_A20_kb

   call check_a20
   cmp ax, 0
   jne .end

   call enable_A20_fast_gate
   
   call check_a20
   cmp ax, 0
   jne .end

   .end:
   ret
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


[BITS 32]

complete_flush: ; this is located at 0x1000

   ; 0x10:
   ; 0000000000010     0         00
   ; index 2 (data)   GDT    privileged   
   
   mov ax, 0x10
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax
   mov ss, ax

   ; Now the kernel is at 0x20E00 - 0x9FFFF (512 KiB)
   ; Copy it to its standard location, 0x100000 (1 MiB)
   
   mov esi, 0x20E00
   mov edi, 0x100000

   mov ecx, 131072 ; 128 K * 4 bytes = 512 KiB
   rep movsd ; copies 4 * ECX bytes from [DS:ESI] to [ES:EDI]
   
   mov esp, 0x1FFFFF ; 1 MB of stack for the kernel
   
   ; xchg bx, bx ; bochs magic break   
   jmp dword 0x08:0x00100000

times 1024-($-complete_flush) db 0   ; Pad to 1 KB   
times 3584-($-$$) db 0               ; Pad to (4 KB - 512 B) in order to the bootloader to be in total 4 KB
