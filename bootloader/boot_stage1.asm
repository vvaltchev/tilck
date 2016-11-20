

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
   mov sp, 0x1FF0
   sti                         ; Restore interrupts

   mov ax, BASE_LOAD_SEG      ; Set data segment to where we're loaded
   mov ds, ax



   ; set video mode
   mov ah, 0x0 ; set video mode
   mov al, 0x3 ; 80x25 mode
   int 0x10
   
   
   mov [current_device], dl
  
   mov si, hello
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
   

   mov ax, [CylindersCount]  
   call print_num
   
   mov ax, [HeadsPerCylinder]  
   call print_num

   mov ax, [SectorsPerTrack]  
   call print_num

   
   
   ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;   
     
   .load_loop:

   ;xchg bx, bx ; magic break
   
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

   mov ah, 2         ; Params for int 13h: read sectors
   mov al, SECTORS_TO_READ_AT_TIME

   ; save the CHS parameters
   mov [saved_cx], cx
   mov [saved_dx], dx
   
   int 13h
   
   ;xchg bx, bx ; magic break
   
   jc .load_error

   mov ax, [currSectorNum]
   add ax, SECTORS_TO_READ_AT_TIME
   mov [currSectorNum], ax
   
   sub ax, 1
   and ax, 0x7F
   cmp ax, 0
   jne .load_loop
   
   ; xchg bx, bx ; magic break

   mov ax, [currDataSeg] 
   cmp ax, 0x9FE0 ; so, we'd have 0x20000 - 0x9FFFF for the kernel (512 KB)
   je .load_OK
   
   add ax, 0x1000
   mov [currDataSeg], ax
   jmp .load_loop
   
.load_error:


   mov si, load_failed
   call print_string
   
   mov ax, [currSectorNum]
   call print_num
   
   mov si, cyl_param
   call print_string
   
   mov cx, [saved_cx]
   xor ax, ax
   mov al, ch  
   call print_num

   mov si, head_param
   call print_string

   mov dx, [saved_dx]
   xor ax, ax
   mov al, dh
   call print_num

   mov si, sector_param
   call print_string

   
   mov cx, [saved_cx]
   xor ax, ax
   mov al, cl
   call print_num
      

   ; continue to boot anyway since with hdd we fail after loading 270 sectors
   ; which are enough for the moment, but that's not OK of course.
   
     
   ; burn some cycles to wait before booting
   
   xor eax, eax
   
   .loop:
   dec eax
   test eax, eax
   jne .loop      ; JMP if EAX != 0  
 
.load_OK:

   ; xchg bx, bx ; magic break   
   jmp DEST_DATA_SEGMENT:0x0000
   
end:
   jmp end

lba_to_chs:         ; Calculate head, track and sector settings for int 13h
                    ; IN: logical sector in AX, OUT: correct registers for int 13h
   push bx
   push ax

   mov bx, ax        ; Save logical sector

   xor dx, dx         ; First the sector
   div word [SectorsPerTrack]
   add dl, 01h       ; Physical sectors start at 1
   mov cl, dl        ; Sectors belong in CL for int 13h
   mov ax, bx

   xor dx, dx         ; Now calculate the head
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

   push strBuf
   push ax ; the input number
   call itoa
   add sp, 4
   
   mov si, strBuf
   call print_string
   
   mov si, newline
   call print_string
  
   ret
   
   
print_string:

   mov ah, 0x0E    ; int 10h 'print char' function

.repeat:
   lodsb           ; Get character from string
   test al, al
   je .done        ; If char is zero, end of string
   int 10h         ; Otherwise, print it
   jmp .repeat

.done:
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
   cmp cx, 0
   jne .loop

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
hello                db 'Hello', 10, 13, 0
load_ok              db 'Load OK', 10, 13, 0
load_failed          db 'Load failed, LBA: ', 0
cyl_param            db 'C: ', 0
head_param           db 'H: ', 0
sector_param         db 'S: ', 0
read_params_failed   db 'F0', 10, 13, 0

current_device       dw 0

currSectorNum        dw 1

                     ; Hack the destination segment in a way to avoid
                     ; special calculations in order to skip the first sector
                     ; of the floppy.
currDataSeg          dw (DEST_DATA_SEGMENT - 512/16)

strBuf               times 8 db 0

;driveParams          dw 0x1E ; size of this buffer
;infoFlags            dw 0
;cyl_count            dd 0
;head_count           dd 0
;sec_per_track        dd 0
;tot_sec              dq 0
;sector_size          dw 0
;edd_params           dd 0


times 510-($-$$) db 0   ; Pad remainder of boot sector with 0s
dw 0xAA55               ; The standard PC boot signature
