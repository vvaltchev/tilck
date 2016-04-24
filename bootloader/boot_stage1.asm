

[BITS 16]
[ORG 0x0000]

%define BASE_LOAD_SEG 0x07C0
%define SECTORS_TO_READ_AT_TIME 1
%define DEST_DATA_SEGMENT 0x2000

start:
   
   mov ax, BASE_LOAD_SEG
   add ax, (8192 / 16)         ; 8K buffer
   cli                         ; Disable interrupts while changing stack
   mov ss, ax
   mov sp, 0x1FFF
   sti                         ; Restore interrupts

   mov ax, BASE_LOAD_SEG      ; Set data segment to where we're loaded
   mov ds, ax



   ; set video mode
   mov ah, 0x0 ; set video mode
   mov al, 0x3 ; 80x25 mode
   int 0x10

   ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;   
   
   mov word [currSectorNum], 1 ; sector 1 (512 bytes after this bootloader)
   
   .big_load_loop:
   
   .load_loop:

   ; xchg bx, bx ; magic break

   mov ax, [currSectorNum]
   call lba_to_chs

   mov ax, [currDataSeg]
   mov es, ax        ; Store currDataSeg in ES, the destination address of the sectors read
                     ; (AX is used since we cannot store directly IMM value in ES)
                     
   mov bx, [currSectorNum]
   shl bx, 9         ; Sectors read from floppy are stored in ES:BX
                     ; bx = 512 * counter * bx
   
   ; 20-bit address in 8086 (real mode)
   ; SEG:OFF
   ; ADDR20 = (SEG << 4) | OFF

   mov ah, 2         ; Params for int 13h: read floppy sectors
   mov al, SECTORS_TO_READ_AT_TIME

   int 13h
   
   ;xchg bx, bx ; magic break
   
   jc .load_error

   mov ax, [currSectorNum]
   add ax, SECTORS_TO_READ_AT_TIME
   mov [currSectorNum], ax
   
   sub ax, 1
   and ax, 0x7F
   cmp ax, 0
   je .end_small_load_loop 
   jmp .load_loop

   .end_small_load_loop:

   ; xchg bx, bx ; magic break

   mov ax, [currDataSeg] 
   cmp ax, 0x9FE0 ; so, we'd have 0x20000 - 0x9FFFF for the kernel (512 KB)
   je .load_OK
   
   add ax, 0x1000
   mov [currDataSeg], ax
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

   xchg bx, bx ; magic break   
   jmp DEST_DATA_SEGMENT:0x0000
   
end:
   jmp end

lba_to_chs:         ; Calculate head, track and sector settings for int 13h
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
   div word [HeadsPerCylinder]
   mov dh, dl        ; Head
   mov ch, al        ; Cylinder

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
HeadsPerCylinder     dw 2
str1                 db 'This is my bootloader!', 10, 13, 0
err1                 db 'ERROR while loading kernel!', 10, 13, 0
newline              db 10, 13, 0
currSectorNum        dw 0

                     ; Hack the destination segment in a way to avoid
                     ; special calculations in order to skip the first sector
                     ; of the floppy.
currDataSeg          dw (DEST_DATA_SEGMENT - 512/16)

times 510-($-$$) db 0   ; Pad remainder of boot sector with 0s
dw 0xAA55               ; The standard PC boot signature
