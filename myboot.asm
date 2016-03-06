

[BITS 16]
[ORG 0x0000]

%define BASE_LOAD_ADDR 0x07C0
%define SECTORS_TO_READ_AT_TIME 8
%define DEST_SEGMENT_KERNEL 0x2000

start:
   
   mov ax, BASE_LOAD_ADDR      ; Set up 4K of stack space above buffer
   add ax, 544                 ; 8k buffer = 512 paragraphs + 32 paragraphs (loader)
   cli                         ; Disable interrupts while changing stack
   mov ss, ax
   mov sp, 4096
   sti                         ; Restore interrupts

   mov ax, BASE_LOAD_ADDR      ; Set data segment to where we're loaded
   mov ds, ax



   ; set video mode
   mov ah, 0x0 ; set video mode
   mov al, 0x3 ; 80x25 mode
   int 0x10

   ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;   
   
   mov word [counter], 1
   
   .big_load_loop:
   
   .load_loop:

   mov ax, [counter]    ; sector 1 (next 512 bytes after this bootloader) 
   call l2hts

   mov ax, [currSegmentKernel]
   mov es, ax        ; Store currSegmentKernel in ES, the destination address of the sectors read
                     ; (AX is used since we cannot store directly IMM value in ES)
                     
   mov bx, [counter]
   shl bx, 9         ; Sectors read from floppy are stored in ES:BX
   sub bx, 512       ; => (bx << 9) - 512;
   
   ; 20 address in 8086 (real mode)
   ; SEG:OFF
   ; ADDR20 = (SEG << 4) | OFF

   mov ah, 2         ; Params for int 13h: read floppy sectors
   mov al, SECTORS_TO_READ_AT_TIME

   int 13h
   
   jc .load_error

   mov ax, [counter]    
   add ax, SECTORS_TO_READ_AT_TIME
   mov [counter], ax
   
   cmp ax, 128
   jge .end_small_load_loop 
   jmp .load_loop
   
   .end_small_load_loop:
   mov ax, [currSegmentKernel]
   
   cmp ax, 0x9000 ; so, we'd have 0x20000 - 0x9FFFF for the kernel (512 KB)
   je .load_OK
   
   add ax, 0x1000
   mov [currSegmentKernel], ax
   jmp .big_load_loop
   
.load_error:

   push err1
   call print_string
   add sp, 2
   jmp end

.load_OK:

   push str1
   call print_string
   add sp, 2 

   jmp DEST_SEGMENT_KERNEL:0x0000
   
end:
   jmp end

l2hts:         ; Calculate head, track and sector settings for int 13h
               ; IN: logical sector in AX, OUT: correct registers for int 13h
   push bx
   push ax

   mov bx, ax        ; Save logical sector

   mov dx, 0         ; First the sector
   div word [SectorsPerTrack]
   add dl, 01h       ; Physical sectors start at 1
   mov cl, dl        ; Sectors belong in CL for int 13h
   mov ax, bx

   mov dx, 0         ; Now calculate the head
   div word [SectorsPerTrack]
   mov dx, 0
   div word [Sides]
   mov dh, dl        ; Head/side
   mov ch, al        ; Track

   pop ax
   pop bx

   mov dl, 0      ; Set correct device

   ret

print_string:      ; Routine: output string in SI to screen
   push bp
   mov bp, sp
   
   mov si, [bp+4]  ; bp+4 is the first argument
   mov ah, 0Eh     ; int 10h 'print char' function

.repeat:
   lodsb           ; Get character from string
   cmp al, 0
   je .done        ; If char is zero, end of string
   int 10h         ; Otherwise, print it
   jmp .repeat

.done:
   leave
   ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

SectorsPerTrack      dw 18    ; Sectors per track (36/cylinder)
Sides                dw 2
str1                 db 'This is my bootloader!', 10, 13, 0
err1                 db 'ERROR while loading kernel!', 10, 13, 0
newline              db 10, 13, 0
counter              dw 0
currSegmentKernel    dw DEST_SEGMENT_KERNEL

times 510-($-$$) db 0   ; Pad remainder of boot sector with 0s
dw 0xAA55               ; The standard PC boot signature