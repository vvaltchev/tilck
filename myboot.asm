   

   BITS 16

start:
   
   mov ax, 07C0h        ; Set up 4K of stack space above buffer
   add ax, 544          ; 8k buffer = 512 paragraphs + 32 paragraphs (loader)
   cli                  ; Disable interrupts while changing stack
   mov ss, ax
   mov sp, 4096
   sti                  ; Restore interrupts

   mov ax, 07C0h        ; Set data segment to where we're loaded
   mov ds, ax

   ;;;;;;;;;;;;;;;;;
   ; EXAMPLE: print a char using BIOS
   ;
   ; mov ah, 0Eh
   ; mov al, 'k'

   ; int 10h
   ;;;;;;;;;;;;;;;;;

   ; set video mode
   mov ah, 0x0 ; set video mode
   mov al, 0x3 ; 80x25 mode
   int 0x10

   ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;   
   
   mov word [counter], 1
   
   

   mov ax, [counter]    ; sector 1 (next 512 bytes after this bootloader)
   call l2hts

   mov ax, 0x2000
   mov es, ax        ; Store 2000h in ES, the destination address of the sectors read
                     ; (cannot store directly IMM value in ES)
   mov bx, 0         ; Store in BX 0, since the sectors are stored in ES:BX
   
   ; 20 address in 8086 (real mode)
   ; SEG:OFF
   ; ADDR20 = (SEG << 4) | OFF


   mov ah, 2         ; Params for int 13h: read floppy sectors
   mov al, 18        ; Sectors to read(10)

   int 13h

   jc error
  
   
   jmp ok

error:

   push err1
   call print_string
   add sp, 2
   jmp end

ok:

   push str1
   call print_string
   add sp, 2
 
   ;mov ah, 0x0
   ;int 0x16 ; read char from keyboard

   jmp 0x2000:0000
   
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

times 510-($-$$) db 0   ; Pad remainder of boot sector with 0s
dw 0xAA55               ; The standard PC boot signature
  
times 1024 db 0