

[BITS 16]
[ORG 0x0000]

%define BASE_LOAD_SEG 0x07C0
%define DEST_DATA_SEGMENT 0x2000

; We're OK with just 1000 512-byte sectors (500 KB)
%define SECTORS_TO_READ 1000

start:

   mov ax, BASE_LOAD_SEG
   add ax, (8192 / 16)         ; 8K buffer
   cli                         ; Disable interrupts while changing stack
   mov ss, ax
   mov sp, 0x1FF0
   sti                         ; Restore interrupts

   mov ax, BASE_LOAD_SEG      ; Set data segment to where we're loaded
   mov ds, ax



   ; set video mode
   mov ah, 0x0 ; set video mode
   mov al, 0x3 ; 80x25 mode
   int 0x10


   mov [current_device], dl

   mov ah, 0x00  ; reset device
   int 0x13
   jc end
   
   .reset_ok:
   
   mov si, dev
   call print_string
   mov ax, [current_device]
   call print_num

   xor ax, ax
   mov es, ax
   mov di, ax

   mov dl, [current_device]
   mov ah, 0x8 ; read drive parameters
   int 0x13

   jnc after_read_params_ok

   mov si, read_params_failed
   call print_string
   jmp end

   after_read_params_ok:

   xor ax, ax
   mov al, dh
   inc al

   mov [HeadsPerCylinder], ax

   mov ax, cx
   and ax, 63   ; last 6 bits
   mov [SectorsPerTrack], ax

   xor ax, ax
   mov al, ch  ; higher 8 bits of CX = lower bits for cyclinders count
   and cx, 192 ; bits 6 and 7 of CX = higher 2 bits for cyclinders count
   shl cx, 8
   or ax, cx
   inc ax
   mov [CylindersCount], ax


   ; mov ax, [CylindersCount]  ; we have already the value in ax
   call print_num

   mov ax, [HeadsPerCylinder]
   call print_num

   mov ax, [SectorsPerTrack]
   call print_num



   ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

   .load_loop:

   mov ax, [currSectorNum]
   call lba_to_chs

   mov ax, [currDataSeg]
   mov es, ax        ; Store currDataSeg in ES, the destination address of the sectors read
                     ; (AX is used since we cannot store directly IMM value in ES)

   mov bx, [currSectorNum]
   shl bx, 9         ; Sectors read are stored in ES:BX
                     ; bx = 512 * counter * bx

   ; 20-bit address in 8086 (real mode)
   ; SEG:OFF
   ; ADDR20 = (SEG << 4) | OFF

   mov ah, 0x02      ; Params for int 13h: read sectors
   mov al, 1         ; Read just 1 sector at time

   ; save the CHS parameters for error messages
   mov [saved_cx], cx
   mov [saved_dx], dx

   int 13h
   jc .load_error

   mov ax, [currSectorNum]

   ; We read all the sectors we needed: loading is over.
   cmp ax, SECTORS_TO_READ
   je .load_OK

   inc ax                    ; we read just 1 sector at time
   mov [currSectorNum], ax

   ; If the current sector num have the bits 0-7 unset,
   ; we loaded 128 sectors * 512 bytes = 64K.
   ; We have to change the segment in order to continue.

   dec ax
   and ax, 0x7F
   test ax, ax
   jne .load_loop ; JMP if ax != 0

   mov ax, [currDataSeg]
  
   ; The idea of reading exactly 512 KB was good but, for some reason,
   ; on my PC, when booting using a USB stick, I cannot read more than
   ; 1022 sectors. So, that's why this code has been commented.

   ;cmp ax, 0x8FE0 ; so, we'd have 0x20000 - 0x9FFFF for the kernel (512 KB)
   ;je .load_OK

   ; Increment the segment by 4K => 64K in plain address space
   add ax, 0x1000
   mov [currDataSeg], ax
   jmp .load_loop

.load_error:

   ; The load failed for some reason

   mov ah, 0x01 ; Get Status of Last Drive Operation
   mov dl, [current_device]
   int 0x13

   ; The carry flag here is always set and people on os-dev say
   ; that one should not rely on it.
   
   ; We have now in AH the last error
   shr ax, 8 ; move AH in AL and make AH=0
   mov si, last_op_status
   call print_string
   call print_num
   
   
   ; Print the sector number (LBA)
   mov si, load_failed
   call print_string
   mov ax, [currSectorNum]
   call print_num

   ; Print the CHS params we actually used
   call print_chs

   ; unrecovable error: hang forever!
   jmp end

.load_OK:
   jmp DEST_DATA_SEGMENT:0x0000

end:
   jmp end

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Utility functions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

print_chs:
   pusha

   ; print the cylinder param
   mov si, cyl_param
   call print_string
   mov ax, [saved_cx]
   shr ax, 8     ; put 'ch' in al
   call print_num

   ; print the head param
   mov si, head_param
   call print_string
   mov ax, [saved_dx]
   shr ax, 8    ; put 'dh' in al
   call print_num

   ; print the sector param
   mov si, sector_param
   call print_string
   mov ax, [saved_cx]
   and ax, 63      ; only the first 6 bits matter
   call print_num

   popa
   ret

lba_to_chs:         ; Calculate head, track and sector settings for int 13h
                    ; IN: logical sector in AX, OUT: correct registers for int 13h
   push bx
   push ax

   mov bx, ax        ; Save logical sector

   ; DIV {ARG}
   ; divides DX:AX by {ARG}
   ; quotient => AX
   ; reminder => DX


   xor dx, dx        ; First the sector
   div word [SectorsPerTrack]
   inc dl            ; Physical sectors start at 1
   mov cl, dl        ; Sectors belong in CL for int 13h
   and cl, 63        ; Make sure the upper two bits of CL are unset


   mov ax, bx        ; reload the LBA sector in AX

   xor dx, dx        ; reset DX and calculate the head
   div word [SectorsPerTrack]
   xor dx, dx
   div word [HeadsPerCylinder]
   mov dh, dl        ; Head
   mov ch, al        ; Cylinder

   pop ax
   pop bx

   mov dl, [current_device]      ; Set correct device

   ret

print_num:

   pusha

   push strBuf
   push ax ; the input number
   call itoa
   add sp, 4

   mov si, strBuf
   call print_string

   mov si, newline
   call print_string

   popa
   ret


print_string:

   push ax         ; save AX for the caller
   
   mov ah, 0x0E    ; int 10h 'print char' function

.repeat:
   lodsb           ; Get character from string
   test al, al
   je .done        ; If char is zero, end of string
   int 10h         ; Otherwise, print it
   jmp .repeat

.done:
   pop ax
   ret

itoa: ; convert 16-bit integer to string

;  USAGE:
;  push destbuffer
;  push number
;
;  call itoa
;  add sp, 4

   push bp
   mov bp, sp
   sub sp, 24

   mov [bp-2], bp
   sub word [bp-2], 4

   .loop:

   xor dx, dx
   mov ax, [bp+4]
   mov bx, 10
   div bx
   mov [bp+4], ax

   mov bx, [bp-2]
   add dl, 48
   mov [bx], dl
   add word [bp-2], 1

   mov cx, [bp+4]
   test cx, cx
   jne .loop   ; JMP if cx != 0

   mov bx, [bp-2]
   sub bx, 1


   mov di, [bp+6]

   .l2:

   mov ax, [bx]
   mov [di], ax

   dec bx
   inc di

   mov ax, bp
   sub ax, 4
   cmp bx, ax
   jge .l2

   mov byte [di], 0

   leave
   ret


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

SectorsPerTrack      dw 0
HeadsPerCylinder     dw 0
CylindersCount       dw 0

saved_cx             dw 0
saved_dx             dw 0

newline              db 10, 13, 0
dev                  db 'Dev:', 0
load_failed          db 'LBA:', 0
cyl_param            db 'C:', 0
head_param           db 'H:', 0
sector_param         db 'S:', 0
last_op_status       db 'LOS:', 0
read_params_failed   db 'F1', 10, 13, 0

current_device       dw 0
currSectorNum        dw 1

                     ; Hack the destination segment in a way to avoid
                     ; special calculations in order to skip the first sector
                     ; of the floppy.
currDataSeg          dw (DEST_DATA_SEGMENT - 512/16)

strBuf               times 8 db 0

times 510-($-$$) db 0   ; Pad remainder of boot sector with 0s
dw 0xAA55               ; The standard PC boot signature
