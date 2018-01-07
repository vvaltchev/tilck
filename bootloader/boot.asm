
[BITS 16]
[ORG 0x0000]

%define VALUE_64K 0x10000

%define BASE_LOAD_SEG 0x07C0
%define DEST_DATA_SEGMENT 0x2000
%define TEMP_DATA_SEGMENT 0x1000
%define RAMDISK_PADDR 0x08000000 ; + 128 MB
%define KERNEL_PADDR  0x00100000 ; +   1 MB

%define RAMDISK_FIRST_SECTOR 2048

; TODO: fix the number of sectors, since the fatpart is now bigger!
; This would require to fix the function lba_to_chs to work with LBA addresses
; bigger than 65535.

; 2048 + 32256 sectors (~16 MB) - 1
;%define RAMDISK_LAST_SECTOR 34304   ; temporary lie!

; DEBUG VALUE, usable until everything fits in 4 MB
; 2048 + 8192 sectors (~4 MB) - 1
%define RAMDISK_LAST_SECTOR 10239


; We're OK with just 1000 512-byte sectors (500 KB)
%define INITIAL_SECTORS_TO_READ 1000

jmp start
times 0x0B - ($-$$) nop

bios_parameter_pack:

sectorsize              dw 512
sectors_per_cluster     db 1
reserved_sectors        dw 2048
number_of_FATs          db 2
root_entries            dw 240
small_sector_count      dw 0    ; the other value large_total_sectors is used.
media_descriptor        db 0xF0 ; floppy (even if that's not a real floppy)

sectors_per_FAT         dw 9

phys_sectors_per_track  dw 63
num_heads               dw 16
hidden_sectors          dd 2048
large_total_sectors     dd (40*1024*1024)/512

drive_number            db 0x80
bflags                  db 0
boot_signature          db 28h  ; DOS 3.4 EBPB
serial_num              dd 123456789

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
   mov sp, 0xFFF0
   sti             ; Restore interrupts

   mov ax, DEST_DATA_SEGMENT ; Update ds to match the new cs segment.
   mov ds, ax

   mov [current_device], dl ; Save the current device

   mov si, str_loading
   call print_string

   .reset_ok:

   xor ax, ax
   mov es, ax
   mov di, ax

   mov dl, [current_device]
   mov ah, 0x8 ; read drive parameters
   int 0x13

   jc end

   after_read_params_ok:

   xor ax, ax
   mov al, dh
   inc al      ; DH contains MAX head num, so we have to add +1.
   mov [heads_per_cylinder], ax

   mov ax, cx
   and ax, 63   ; last 6 bits
   mov [sectors_per_track], ax ; Actual number of sectors, NOT number_of - 1.

   xor ax, ax
   mov al, ch  ; higher 8 bits of CX = lower bits for cyclinders count
   and cx, 192 ; bits 6 and 7 of CX = higher 2 bits for cyclinders count
   shl cx, 8
   or ax, cx
   inc ax      ; the 10 bits in CX are the MAX cyl number, so we have to add +1.
   mov [cylinders_count], ax


   .load_loop:


   mov ax, [curr_sec]
   call lba_to_chs

   mov ax, [curr_data_seg]
   mov es, ax

   mov bx, [curr_sec]
   shl bx, 9         ; Sectors read are stored in ES:BX
                     ; bx *= 512 * curr_sec

   mov ah, 0x02      ; Params for int 13h: read sectors
   mov al, 1         ; Read just 1 sector at time


   int 13h
   jc end

   mov ax, [curr_sec]

   ; We read all the sectors we needed: loading is over.
   cmp ax, INITIAL_SECTORS_TO_READ
   je .load_OK

   inc ax                    ; we read just 1 sector at time
   mov [curr_sec], ax

   ; If the current sector num have the bits 0-7 unset,
   ; we loaded 128 sectors * 512 bytes = 64K.
   ; We have to change the segment in order to continue.

   and ax, 0x7F
   test ax, ax
   jne .load_loop ; JMP if ax != 0

   mov ax, [curr_data_seg]


   ; Increment the segment by 4K => 64K in plain address space
   add ax, 0x1000
   mov [curr_data_seg], ax
   jmp .load_loop

.load_OK:
   jmp DEST_DATA_SEGMENT:stage2_entry

end:
   mov si, str_failed
   call print_string
   jmp $ ; loop forever


; MBR data

times 218 - ($-$$) nop      ; Pad for disk time stamp

times 6 db 0  ; Disk Time Stamp (aka "mistery bytes")
              ; See http://thestarman.pcministry.com/asm/mbr/mystery.htm

times 224 - ($-$$) db 0     ; Pad for the beginning of the 2nd code area.

;
;
; SOME SPACE FOR CODE and DATA
;
;

; -----------------------------------------------------------
; DATA (variables)
; -----------------------------------------------------------

sectors_per_track    dw 0
heads_per_cylinder   dw 0
cylinders_count      dw 0

curr_data_seg        dw DEST_DATA_SEGMENT
current_device       dw 0
curr_sec             dd 1

str_loading          db 'Loading... ', 0
str_failed           db 'FAILED', 13, 10, 0

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Utility functions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


lba_to_chs:         ; Calculate head, track and sector settings for int 13h
                    ; IN: LBA in AX, OUT: correct registers for int 13h
   push bx
   push ax

   mov bx, ax        ; Save logical sector

   ; DIV {ARG}
   ; divides DX:AX by {ARG}
   ; quotient => AX
   ; reminder => DX


   xor dx, dx        ; First the sector
   div word [sectors_per_track]
   inc dl            ; Physical sectors start at 1
   mov cl, dl        ; Sectors belong in CL for int 13h
   and cl, 63        ; Make sure the upper two bits of CL are unset


   mov ax, bx        ; reload the LBA sector in AX

   xor dx, dx        ; reset DX and calculate the head
   div word [sectors_per_track]
   xor dx, dx
   div word [heads_per_cylinder]
   mov dh, dl        ; Head
   mov ch, al        ; Cylinder

   pop ax
   pop bx

   mov dl, [current_device]      ; Set correct device

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

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


times 436 - ($-$$) nop      ; Pad For MBR Partition Table

UID: ; Unique Disk ID
db 0x00, 0x00, 0x00, 0x00, 0x2b, 0x06, 0x06, 0x49, 0x00, 0x00


PT1: ; First Partition Entry

; A 35MB FAT32 partition, from sector 2048 to sector 73727.

; status: 0x80 means active/bootable, 0x00 means inactive.
db 0x80 ; it doesn't really matter in our case.

; first absolute sector (CHS) of the partition, 3 bytes
; in this case, it is: 2048

; C = LBA / (heads_per_cyl * sectors_per_track)
; H = (LBA / sectors_per_track) % heads_per_cyl
; S = (LBA % sectors_per_track) + 1
;
; LBA = (C × heads_per_cyl + H) × sectors_per_track + (S - 1)

; Given our (typical LBA) values:
; heads_per_cyl = 16
; sectors_per_track = 63
;
; C = LBA / (16*63)
; H = (LBA / 63) % 16
; S = (LBA % 63) + 1

db  0 ; head
db 33 ; HI cyl num  | sector num
      ; bits [7-6]  | bits [5-0]

db  2 ; LO 8 bits of the cylinder num

; partition type
db 0x0C ; FAT32 (LBA)

; last abs sector (CHS), 3 bytes
; it this case it is: 73727

db  2 ; head
db 18 ; sector + 2 HI bits of cyl (0)
db 73 ; cylinder (lower 8 bits)

; LBA first sector in the partition
dd 0x00000800 ; 2048
dd 0x00012000 ; 71680 sectors: 35 MB


PT2 times 16 db 0             ; Second Partition Entry
PT3 times 16 db 0             ; Third Partition Entry
PT4 times 16 db 0             ; Fourth Partition Entry

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

   ; Set all segments to match where this code is loaded
   mov ax, DEST_DATA_SEGMENT
   mov es, ax
   mov fs, ax
   mov gs, ax

   mov ah, 0x0 ; set video mode
   mov al, 0x3 ; 80x25 mode
   int 0x10

   ; Hello message, just a "nice to have"
   mov si, str_hello
   call print_string

   mov si, str_device
   mov ax, [current_device]
   call print_string_and_num

   mov si, str_cylinders
   mov ax, [cylinders_count]
   call print_string_and_num

   mov si, str_heads_per_cyl
   mov ax, [heads_per_cylinder]
   call print_string_and_num

   mov si, str_sectors_per_track
   mov ax, [sectors_per_track]
   call print_string_and_num

   ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

   cli          ; disable interrupts

   call smart_enable_A20

   jmp enter_unreal_mode

   saved_cx dw 0
   saved_dx dw 0

   error_occured dd 0
   sectors_read dd 0
   bytes_per_track dd 0
   ramdisk_dest_addr32 dd RAMDISK_PADDR


   gdt:
   gdt_null db 0, 0, 0, 0, 0, 0, 0, 0
   gdt_code db 0xFF, 0xFF, 0, 0, 0, 0x9A, 0xCF, 0
   gdt_data db 0xFF, 0xFF, 0, 0, 0, 0x92, 0xCF, 0

   gdtr:
      dw 0x0023
      dd 0x00000000

   idtr:
      dw 0x0000
      dd 0x00000000

   small_buf            times 8 db 0

   load_failed          db 'LBA: ', 0
   cyl_param            db 'C: ', 0
   head_param           db 'H: ', 0
   sector_param         db 'S: ', 0
   last_op_status       db 'Last op status: ', 0

   newline db 13, 10, 0
   str_device            db 'Device number: ', 0
   str_cylinders         db 'Cyclinders count:   ', 0
   str_heads_per_cyl     db 'Heads per cylinder: ', 0
   str_sectors_per_track db 'Sectors per track:  ', 0
   str_hello db 'Hello, I am the 2nd stage-bootloader!', 13, 10, 0
   err_while_loading_ramdisk db 'Error while loading ramdisk', 13, 10, 0
   str_load_of_ramdisk_completed db 'Loading of ramdisk completed.', 13, 10, 0
   str_before_reading_curr_sec db 'Current sector num: ', 0
   str_curr_sector_num db 'After reading, current sector: ', 0
   str_bytes_per_track db 'Bytes per track: ', 0
   str_loading_ramdisk db 'Loading ramdisk ', 0

enter_unreal_mode:

   ; calculate the absolute 32 bit address of GDT
   ; since flat addr = SEG << 4 + OFF
   ; that's exactly what we do below (SEG is DS)

   xor eax, eax
   mov ax, cs
   shl eax, 4
   add eax, gdt
   mov dword [gdtr+2], eax


   lgdt [gdtr]            ; load gdt register

   mov eax, cr0           ; switch to 16-bit pmode by
   or al, 1                ; set pmode bit
   mov cr0, eax

   jmp $+2                ; tell 386/486 to not crash

   mov bx, 0x10           ; select descriptor 2
   mov es, bx             ; store it in 'es'.
                          ; After that, only it can be used for indexing
                          ; 32-bit addresses from "unreal mode".

   and al, 0xFE           ; back to realmode
   mov cr0, eax           ; by toggling bit again

   ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

   sti ; re-enable interrupts

   mov si, str_loading_ramdisk
   call print_string

   mov dword [curr_sec], RAMDISK_FIRST_SECTOR

   xor edx, edx
   mov dx, [sectors_per_track]
   shl dx, 9 ; dx = dx << 9 (2^9 = 512 = sector size)
   mov [bytes_per_track], edx

   .load_vdisk_loop:

   ; Progress by showing dots: faster than full verbose strings

   mov ah, 0x0E ; print char
   mov al, 46   ; dot '.'
   int 0x10

   mov ax, [curr_sec]
   call lba_to_chs
   mov ax, TEMP_DATA_SEGMENT
   mov es, ax        ; set the destination segment
   mov bx, 0         ; set the destination offset

   ; save the CHS parameters for error messages
   mov [saved_cx], cx
   mov [saved_dx], dx

   mov ah, 0x02      ; Params for int 13h: read sectors
   mov al, [sectors_per_track] ; Read MAX possible sectors
   int 0x13
   jnc .read_ok

   .read_error:

   ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
   ; ERROR HANDLING CODE
   ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

   mov word [error_occured], 1
   mov si, err_while_loading_ramdisk
   call print_string

   ; The load failed for some reason

   mov ah, 0x01 ; Get Status of Last Drive Operation
   mov dl, [current_device]
   int 0x13

   ; The carry flag here is always set and people on os-dev say
   ; that one should not rely on it.

   ; We have now in AH the last error
   shr ax, 8 ; move AH in AL and make AH=0
   mov si, last_op_status
   call print_string_and_num


   ; Print the sector number (LBA)
   mov si, load_failed
   mov ax, [curr_sec]
   call print_string_and_num

   ; Print the CHS params we actually used
   call print_chs

   .hang: jmp $

   .read_ok:

   xor ax, ax
   mov es, ax

   mov edi, [ramdisk_dest_addr32]          ; dest flat addr
   mov esi, (TEMP_DATA_SEGMENT * 16)       ; src flat addr

   mov edx, esi
   add edx, [bytes_per_track]

   .copy_loop:
      mov ebx, [es:esi]  ; copy src data in ebx
      mov [es:edi], ebx  ; copy ebx in dest ptr
      add edi, 4
      add esi, 4

      cmp esi, edx
      jle .copy_loop


   mov eax, [ramdisk_dest_addr32]
   add eax, [bytes_per_track]
   mov [ramdisk_dest_addr32], eax


   mov eax, [curr_sec]
   add ax, [sectors_per_track]
   mov [curr_sec], eax

   cmp eax, RAMDISK_LAST_SECTOR
   jl .load_vdisk_loop

   .load_of_ramdisk_done:

   mov si, newline
   call print_string

   mov si, str_load_of_ramdisk_completed
   call print_string

   ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

   enter_32bit_protected_mode:

   cli

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

print_num:

   pusha

   push small_buf
   push ax ; the input number
   call itoa
   add sp, 4

   mov si, small_buf
   call print_string

   mov si, newline
   call print_string

   popa
   ret



; IN: string in SI
; IN: number in AX

print_string_and_num:
   call print_string
   call print_num
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

   mov esi, (DEST_DATA_SEGMENT * 16 + 0x10000) ; 0x10000 = 64 KB for the bootloader
   mov edi, KERNEL_PADDR

   mov ecx, 131072 ; 128 K * 4 bytes = 512 KiB
   rep movsd ; copies 4 * ECX bytes from [DS:ESI] to [ES:EDI]

   ; jump to kernel
   jmp 0x08:KERNEL_PADDR

times 1024-($-complete_flush) db 0   ; Pad to 1 KB. That guarantees us that complete_flush is <= 1 KB.
times 4096-($-$$) db 0               ; Pad to 4 KB in order to the whole bootloader to be exactly 4 KB
