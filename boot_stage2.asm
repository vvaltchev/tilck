
[ORG 0x20000]
[BITS 16]

   ; Our trivial bootloader has loaded this code at absolute address 0x20000
   ; now we have more than 512 bytes to execute

   jmp entry_point ; 3 bytes

gdt0 db 0, 0, 0, 0, 0, 0, 0, 0
gdt1 db 0xFF, 0xFF, 0, 0, 0, 0x9A, 0xCF, 0
gdt2 db 0xFF, 0xFF, 0, 0, 0, 0x92, 0xCF, 0

idt  times 2048 db 0
   
gdtr db 23,      0,  0, 0, 0, 0
idtr db 0xFF, 0x07,  0, 0, 0, 0

helloStr db 'Hello, I am the 2nd stage-bootloader', 13, 10, 0      

 
%INCLUDE 'realModeStrings.asm'


   entry_point:
   
   cli             ; Clear interrupts
   mov ax, 0
   mov ss, ax      ; Set stack segment and pointer
   mov sp, 0FFFFh
   sti             ; Restore interrupts

   cld               ; The default direction for string operations
                     ; will be 'up' - incrementing address in RAM

   mov ax, 2000h     ; Set all segments to match where kernel is loaded
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
   ; since the kernel is loaded in 0x2000:0000,
   ; this means, 0x20000.
   ; the following code write this value (litte-endian)
   ; in to GDTR
   
   mov word [gdtr+2], 0x03
   mov word [gdtr+3], 0x00
   mov word [gdtr+4], 0x02
   mov word [gdtr+5], 0x00

   
   ; xchg bx, bx ; bochs magic break 
     
   mov word [idtr+2], 0x1B ; 0x3 (jmp) + 0x18 (GDT)
   mov word [idtr+3], 0x00
   mov word [idtr+4], 0x02
   mov word [idtr+5], 0x00    
   
   ; now we have to copy the text from
   ; complete_flush + 0x0 to complete_flush + 4 KB
   ; into 0x0000:0x1000
   
   mov si, complete_flush
   mov di, 0x1000
   mov cx, 0
   mov es, cx ; using extra segment for 0x0
   
   .copy_loop:
   
   cmp cx, 4096
   je .end_copy_loop
   add cx, 2
   
   mov bx, [si]  
   mov [es:di], bx 
   
   add si, 2
   add di, 2
   
   jmp .copy_loop
   
   .end_copy_loop:
   
   
   ;xchg bx, bx ; bochs magic break 

   lidt [idtr]
   
   
   ; layout of segments:
   ; http://stackoverflow.com/questions/23978486/far-jump-in-gdt-in-bootloader

   ; index (13b) | TI (2b) | RPL (2b)
   ; TI = table indicator; 0 = GDT, 1 = LDT
   ; RPL = Requestor priviledge level; 00 = highest, 11 = lowest
   
flush_gdt:
   lgdt [gdtr]  ; load GDT register with start address of Global Descriptor Table
   
   
   jmp 0x08:0xF80 ; the JMP sets CS (code selector);
                  ; the JMP jumps at OFFSET + 0x80;
                  ; 0xF80 is 0x1000 - 0x80
                  ; TODO: Why?? This should not happen!

   ; 0x08 =
   ; 0000000000001     0         00
   ; index 1 (code)   GDT    privileged

   
complete_flush: ; will be copied at 0x1000
   mov ax, 0x10
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax
   mov ss, ax

   ; 0x10:
   ; 0000000000010     0         00
   ; index 2 (data)   GDT    privileged   
   
   ;xchg bx, bx ; bochs magic break   

   call smart_enable_A20

   ;xchg bx, bx ; bochs magic break      

   
   mov eax, cr0 
   or al, 1     ; set PE (Protection Enable) bit in CR0 (Control Register 0)
   mov cr0, eax   

   ; sti
   
   
   xchg bx, bx ; bochs magic break
   
   nop
   nop
   nop
   nop
   
   jmp asmMain


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

asmMain:

   nop
   nop
   nop
   nop
   nop
   nop
   nop
   nop

   jmp 0x22000
   
   ;sti
   
   ; mov eax, 0x07690748
   ; mov [0xb8000], eax
   
   ; mov al, 65
   ; mov ah, 0
   ; mov ebx, 0xb8000
   ; mov [ebx], ax
   

   ;after_loop:
   
   
   end:
      jmp end


times 4096-($-complete_flush) db 0   ; Pad to 4 KB   
times 8192-($-$$) db 0   ; Pad to 8 KB   
