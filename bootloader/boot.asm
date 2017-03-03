
[BITS 16]
[ORG 0x0000]

%define BASE_LOAD_SEG 0x07C0
%define DEST_DATA_SEGMENT 0x2000
%define TEMP_DATA_SEGMENT 0x1000
%define VDISK_ADDR 0x8000000

%define VDISK_FIRST_LBA_SECTOR 1008

; 1008 + 32768 sectors (16 MB) - 1
%define VDISK_LAST_LBA_SECTOR 33775

; Smaller last LBA sector for quick tests
;%define VDISK_LAST_LBA_SECTOR 2000


; We're OK with just 1000 512-byte sectors (500 KB)
%define SECTORS_TO_READ 1000

start:

   cli               ; Clear interrupts
   cld               ; The default direction for string operations
                     ; will be 'up' - incrementing address in RAM


   ; relocate to DEST_DATA_SEGMENT

   mov ax, BASE_LOAD_SEG
   mov ds, ax
   mov ax, DEST_DATA_SEGMENT
   mov es, ax

   xor si, si ; si = 0
   xor di, di ; di = 0
   mov cx, 256 ; 256 words = 512 bytes
   rep movsw

   jmp DEST_DATA_SEGMENT:after_reloc

after_reloc:

   xor ax, ax
   mov ss, ax      ; Set stack segment and pointer
   mov sp, 0x0FFF0
   sti             ; Restore interrupts

   mov ax, DEST_DATA_SEGMENT   ; Set all segments to match where this code is loaded
   mov ds, ax

   mov [current_device], dl ; Save the current device


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

   ; -------------------------------------------
   ; DEBUG CODE
   ; -------------------------------------------

   ; mov ax, [CylindersCount]  ; we have already the value in ax
   call print_num

   mov ax, [HeadsPerCylinder]
   call print_num

   mov ax, [SectorsPerTrack]
   call print_num

   ; ------------------------------------------
   ; END DEBUG CODE
   ; ------------------------------------------


   .load_loop:


   mov ax, [currSectorNum]
   call lba_to_chs

   mov ax, [currDataSeg]
   mov es, ax        ; Store currDataSeg in ES, the destination address of the sectors read
                     ; (AX is used since we cannot store directly IMM value in ES)

   mov bx, [currSectorNum]
   shl bx, 9         ; Sectors read are stored in ES:BX
                     ; bx *= 512 * currSectorNum

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

   and ax, 0x7F
   test ax, ax
   jne .load_loop ; JMP if ax != 0

   mov ax, [currDataSeg]


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

   jmp DEST_DATA_SEGMENT:512

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

; -----------------------------------------------------------
; DATA (variables)
; -----------------------------------------------------------

SectorsPerTrack      dw 0
HeadsPerCylinder     dw 0
CylindersCount       dw 0

saved_cx             dw 0
saved_dx             dw 0

newline              db 10, 13, 0
dev                  db 'D:', 0
load_failed          db 'LBA:', 0
cyl_param            db 'C:', 0
head_param           db 'H:', 0
sector_param         db 'S:', 0
last_op_status       db 'LOS:', 0
read_params_failed   db 'F1', 10, 13, 0

current_device       dw 0
currSectorNum        dw 1

currDataSeg          dw DEST_DATA_SEGMENT

strBuf               times 8 db 0

times 510-($-$$) db 0   ; Pad remainder of boot sector with 0s
dw 0xAA55               ; The standard PC boot signature


; -------------------------------------------------------------
;
; STAGE 2
;
; -------------------------------------------------------------

   ; The code above has loaded this code at absolute address 0x20000
   ; now we have more than 512 bytes to execute.

   stage2_entry:

   mov ax, DEST_DATA_SEGMENT   ; Set all segments to match where this code is loaded
   mov es, ax
   mov fs, ax
   mov gs, ax

   ; set video mode
   mov ah, 0x0 ; set video mode
   mov al, 0x3 ; 80x25 mode
   int 0x10

   ; Hello message, just a "nice to have"
   mov si, helloStr
   call print_string

   ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

   cli          ; disable interrupts

   call smart_enable_A20

   jmp enter_unreal_mode

   gdt16info:
   dw gdt16_end - gdt16 - 1   ; size of table - 1
   dd 0                       ; start of table

   gdt16       dd 0,0        ; entry 0 is always unused
   flatdesc    db 0xff, 0xff, 0, 0, 0, 10010010b, 11001111b, 0
   gdt16_end:


   sec_num dd 0
   error_occured dd 0
   sectors_read dd 0
   vdisk_dest_addr dd VDISK_ADDR


   error_while_loading_vdisk db 'Error while loading vdisk', 10, 13, 0
   read_seg_msg db 'Reading a segment..',10,13,0
   load_of_vdisk_complete db 'Loading of vdisk completed.', 10, 13, 0
   str_before_reading_sec_num db 'Before reading sec num: ', 0
   str_curr_sector_num db 'After reading, current sector: ', 0

   enter_unreal_mode:

   xor eax, eax
   mov ax, ds
   shl eax, 4
   add eax, gdt16
   mov dword [gdt16info+2], eax


   push ds                ; save real mode

   lgdt [gdt16info]       ; load gdt register

   mov eax, cr0           ; switch to 16-bit pmode by
   or al,1                ; set pmode bit
   mov cr0, eax

   jmp $+2                ; tell 386/486 to not crash

   mov bx, 0x08           ; select descriptor 1
   mov ds, bx             ; 8h = 1000b

   and al, 0xFE           ; back to realmode
   mov cr0, eax           ; by toggling bit again

   pop ds                 ; get back old segment

   ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

   ; bochs magic break
   ; xchg bx, bx

   sti ; re-enable interrupts

   mov word [sec_num], VDISK_FIRST_LBA_SECTOR

   read_a_segment_from_drive:

   mov word [sectors_read], 0

   mov si, read_seg_msg
   call print_string

   ; mov si, str_before_reading_sec_num
   ; call print_string

   ; mov ax, [sec_num]
   ; call print_num


   loop_for_reading_a_segment:

   mov ax, [sec_num]
   call lba_to_chs
   mov ax, TEMP_DATA_SEGMENT
   mov es, ax        ; set the destination segment

   mov bx, [sectors_read]
   shl bx, 9 ; bx *= 512    (destination offset)

   mov ah, 0x02      ; Params for int 13h: read sectors
   mov al, 1         ; Read just 1 sector at time
   int 13h
   jc read_error

   mov ax, [sectors_read]
   mov bx, [sec_num]
   inc ax
   inc bx
   mov [sectors_read], ax
   mov [sec_num], bx

   cmp ax, 128 ; = 64 KiB
   je read_segment_done

   jmp loop_for_reading_a_segment

   read_error:

   mov word [error_occured], 1
   mov si, error_while_loading_vdisk
   call print_string

   read_segment_done:

   mov ax, 0
   mov es, ax

   mov eax, [vdisk_dest_addr]  ; dest
   mov ecx, 0x10000            ; src addr

   copy_segment_loop:
      mov ebx, [es:ecx]
      mov [es:eax], ebx
      add eax, 4
      add ecx, 4

      cmp ecx, 0x20000
      jl copy_segment_loop


   mov eax, [vdisk_dest_addr]
   add eax, 0x10000 ; 64 KB
   mov [vdisk_dest_addr], eax


   mov ax, [error_occured]
   cmp ax, 1
   jne continue_load

   ; An error occurred. Show a message?
   jmp load_of_vdisk_done

   continue_load:

   ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

   mov si, str_curr_sector_num
   call print_string

   mov ax, [sec_num]
   call print_num


   ; Use EAX instead of AX since the LBA sector is more than 2^15-1
   mov eax, [sec_num]
   cmp eax, VDISK_LAST_LBA_SECTOR
   jge load_of_vdisk_done

   jmp read_a_segment_from_drive


   load_of_vdisk_done:

   mov si, load_of_vdisk_complete
   call print_string

   ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

   enter_32bit_protected_mode:

   cli

   ; calculate the absolute 32 bit address of GDT
   ; since flat addr = SEG << 4 + OFF
   ; that's exactly what we do below (SEG is DS)

   xor eax, eax
   mov ax, ds
   shl eax, 4
   add eax, gdt
   mov dword [gdtr+2], eax

   ; now we have to copy the text from
   ; complete_flush + 0x0 to complete_flush + 1 KB
   ; into 0x0000:0x1000

   mov si, complete_flush
   mov di, 0x1000
   mov cx, 512 ; 512 2-byte words

   mov ax, 0
   mov es, ax ; using extra segment for 0x0
   rep movsw  ; copies 2*CX bytes from [ds:si] to [es:di]

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

   ; 0x08 is the first selector
   ; 0000000000001     0         00
   ; index 1 (code)   GDT    privileged

   cli
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

gdt:
db 0, 0, 0, 0, 0, 0, 0, 0
db 0xFF, 0xFF, 0, 0, 0, 0x9A, 0xCF, 0
db 0xFF, 0xFF, 0, 0, 0, 0x92, 0xCF, 0

gdtr db 23, 0, 0, 0, 0, 0
idtr db 0, 0, 0, 0, 0, 0

helloStr db 'Hello, I am the 2nd stage-bootloader', 13, 10, 0


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

   ; Copy the kernel to its standard location, 0x100000 (1 MiB)

   mov esi, (DEST_DATA_SEGMENT * 16 + 0x1000) ; 0x1000 = 4 KB for the bootloader
   mov edi, 0x100000

   mov ecx, 131072 ; 128 K * 4 bytes = 512 KiB
   rep movsd ; copies 4 * ECX bytes from [DS:ESI] to [ES:EDI]

   mov esp, 0x1FFFF0 ; 1 MB of stack for the kernel

   ; jump to kernel
   jmp dword 0x08:0x00100000

times 1024-($-complete_flush) db 0   ; Pad to 1 KB. That guarantees us that complete_flush is <= 1 KB.
times 4096-($-$$) db 0               ; Pad to 4 KB in order to the whole bootloader to be exactly 4 KB
