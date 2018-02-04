
.intel_syntax noprefix

#include <config.h>

#define BASE_LOAD_SEG          0x07C0
#define DEST_DATA_SEGMENT      0x2000
#define TEMP_DATA_SEGMENT      0x1000
#define COMPLETE_FLUSH_ADDR    0x1000
#define COMPLETE_FLUSH_SIZE       128


#define RAMDISK_FIRST_SECTOR 2048


#define RAMDISK_LAST_SECTOR (RAMDISK_FIRST_SECTOR + (35*MB)/SECTOR_SIZE - 1)

//DEBUG VALUE, usable until everything fits in 4 MB
//#define RAMDISK_LAST_SECTOR (RAMDISK_FIRST_SECTOR + (4 * MB)/SECTOR_SIZE - 1)


/*
 * We're OK with just 32 KB because now the initial sectors contain just the 3rd
 * stage of the bootloader. The actual kernel file is loaded by the bootloader
 * from the FAT32 ramdisk.
 */

#define INITIAL_SECTORS_TO_READ (32*KB/SECTOR_SIZE)

.org 0x0000

.global _start

.section .text

.code16

_start:

jmp reloc

# Fill the gap with nops since bios_parameter_pack has to be at offset +0x0B.
.space 0x0B - (.-_start)

bios_parameter_pack:

sectorsize:              .word SECTOR_SIZE
sectors_per_cluster:     .byte 1
reserved_sectors:        .word 2048
number_of_FATs:          .byte 1
root_entries:            .word 240
small_sector_count:      .word 0    # the other value large_total_sectors is used.
media_descriptor:        .byte 0xF0 # floppy (even if that's not a real floppy)

sectors_per_FAT:         .word 9

phys_sectors_per_track:  .word 63
num_heads:               .word 16
hidden_sectors:          .long 2048
large_total_sectors:     .long (37*MB)/SECTOR_SIZE

drive_number:            .byte 0x80
bflags:                  .byte 0
boot_signature:          .byte 0x28  # DOS 3.4 EBPB
serial_num:              .long 123456789

reloc:

   cli               # Clear interrupts
   cld               # The default direction for string operations
                     # will be 'up' - incrementing address in RAM

   # relocate to DEST_DATA_SEGMENT

   mov ax, BASE_LOAD_SEG
   mov ds, ax
   mov ax, DEST_DATA_SEGMENT
   mov es, ax

   xor si, si # si = 0
   xor di, di # di = 0
   mov cx, 256 # 256 words = 512 bytes
   rep movsw

   jmp DEST_DATA_SEGMENT:after_reloc

after_reloc:

   xor ax, ax
   mov ss, ax      # Set stack segment and pointer
   mov sp, 0xFFF0
   sti             # Restore interrupts

   mov ax, DEST_DATA_SEGMENT # Update ds to match the new cs segment.
   mov ds, ax

   mov [current_device], dl # Save the current device

   mov si, offset str_loading
   call print_string

   .reset_ok:

   xor ax, ax
   mov es, ax
   mov di, ax

   mov dl, [current_device]
   mov ah, 0x8 # read drive parameters
   int 0x13

   jc end

   .after_read_params_ok:

   xor ax, ax
   mov al, dh
   inc al      # DH contains MAX head num, so we have to add +1.
   mov [heads_per_cylinder], ax

   mov ax, cx
   and ax, 63   # last 6 bits
   mov [sectors_per_track], ax # Actual number of sectors, NOT number_of - 1.

   xor ax, ax
   mov al, ch  # higher 8 bits of CX = lower bits for cyclinders count
   and cx, 192 # bits 6 and 7 of CX = higher 2 bits for cyclinders count
   shl cx, 8
   or ax, cx
   inc ax      # the 10 bits in CX are the MAX cyl number, so we have to add +1.
   mov [cylinders_count], ax


   .load_loop:


   mov eax, [curr_sec]
   call lba_to_chs

   mov ax, [curr_data_seg]
   mov es, ax

   mov bx, [curr_sec]
   shl bx, 9         # Sectors read are stored in ES:BX
                     # bx *= 512 * curr_sec

   mov ah, 0x02      # Params for int 13h: read sectors
   mov al, 1         # Read just 1 sector at time


   int 0x13
   jc end

   mov ax, [curr_sec]

   # We read all the sectors we needed: loading is over.
   cmp ax, INITIAL_SECTORS_TO_READ
   je .load_OK

   inc ax                    # we read just 1 sector at time
   mov [curr_sec], ax

   # If the current sector num have the bits 0-7 unset,
   # we loaded 128 sectors * 512 bytes = 64K.
   # We have to change the segment in order to continue.

   and ax, 0x7F
   test ax, ax
   jne .load_loop # JMP if ax != 0

   mov ax, [curr_data_seg]


   # Increment the segment by 4K => 64K in plain address space
   add ax, 0x1000
   mov [curr_data_seg], ax
   jmp .load_loop

.load_OK:
   jmp DEST_DATA_SEGMENT:stage2_entry

end:
   mov si, offset str_failed
   call print_string

   cli
   hlt # hang forever



# MBR data

.space 218 - (.-_start), 0x00 # Pad for disk time stamp

.space 6, 0x00  # Disk Time Stamp (aka "mistery bytes")
                # See http://thestarman.pcministry.com/asm/mbr/mystery.htm

.space 224 - (.-_start), 0x00 # Pad for the beginning of the 2nd code area.

#
#
# SOME SPACE FOR CODE and DATA
#
#

# -----------------------------------------------------------
# DATA (variables)
# -----------------------------------------------------------

sectors_per_track:    .long 0
heads_per_cylinder:   .long 0
cylinders_count:      .word 0

curr_data_seg:        .word DEST_DATA_SEGMENT
current_device:       .word 0
curr_sec:             .long 1

str_loading:          .asciz "Loading... "
str_failed:           .asciz "FAILED\r\n"


##############################################
# Utility functions
##############################################


lba_to_chs:         # Calculate head, track and sector settings for int 13h
                    # IN:  LBA in EAX
                    # OUT: correct registers for int 13h
   push ebx
   push eax

   mov ebx, eax        # Save logical sector

   # DIV {ARG}
   # divides DX:AX by {ARG}
   # quotient => AX
   # reminder => DX


   xor edx, edx      # First the sector
   div dword ptr [sectors_per_track]
   inc edx           # Physical sectors start at 1
   mov cl, dl        # Sectors belong in CL for int 13h
   and cl, 63        # Make sure the upper two bits of CL are unset


   mov eax, ebx      # reload the LBA sector in AX

   xor edx, edx      # reset DX and calculate the head
   div dword ptr [sectors_per_track]
   xor edx, edx
   div dword ptr [heads_per_cylinder]
   mov dh, dl        # Head
   mov ch, al        # Cylinder

   shr ax, 2         # Move the 2 higher bits of cylinder down
   and ax, 192       # make only bits 6 and 7 to remain
   or cl, al         # OR those two bits in CL, since its 2 upper-bits
                     # are the upper bits of cylinder.

   pop eax
   pop ebx

   mov dl, [current_device]      # Set correct device

   ret


print_string:

   push ax         # save AX for the caller

   mov ah, 0x0E    # int 10h 'print char' function

.repeat:
   lodsb           # Get character from string
   test al, al
   je .done        # If char is zero, end of string
   int 0x10        # Otherwise, print it
   jmp .repeat

.done:
   pop ax
   ret

########################################################################

.space 436 - (.-_start), 0x90      # Pad For MBR Partition Table

UID: # Unique Disk ID
.byte 0x00, 0x00, 0x00, 0x00, 0x2b, 0x06, 0x06, 0x49, 0x00, 0x00


PT1: # First Partition Entry

# A 35MB FAT32 partition, from sector 2048 to sector 73727.

# status: 0x80 means active/bootable, 0x00 means inactive.
.byte 0x80 # it doesn't really matter in our case.

# first absolute sector (CHS) of the partition, 3 bytes
# in this case, it is: 2048

# C = LBA / (heads_per_cyl * sectors_per_track)
# H = (LBA / sectors_per_track) % heads_per_cyl
# S = (LBA % sectors_per_track) + 1
#
# LBA = (C × heads_per_cyl + H) × sectors_per_track + (S - 1)

# Given our (typical LBA) values:
# heads_per_cyl = 16
# sectors_per_track = 63
#
# C = LBA / (16*63)
# H = (LBA / 63) % 16
# S = (LBA % 63) + 1

.byte  0 # head
.byte 33 # HI cyl num  | sector num
         # bits [7-6]  | bits [5-0]

.byte  2 # LO 8 bits of the cylinder num

# partition type
.byte 0x0C # FAT32 (LBA)

# last abs sector (CHS), 3 bytes
# it this case it is: 73727

.byte  2 # head
.byte 18 # sector + 2 HI bits of cyl (0)
.byte 73 # cylinder (lower 8 bits)

# LBA first sector in the partition
.long 0x00000800 # 2048
.long 0x00012000 # 71680 sectors: 35 MB


PT2: .space 16, 0x00             # Second Partition Entry
PT3: .space 16, 0x00             # Third Partition Entry
PT4: .space 16, 0x00             # Fourth Partition Entry

.space 510-(.-_start), 0   # Pad remainder of boot sector with 0s
.word 0xAA55               # The standard PC boot signature



# -------------------------------------------------------------
#
# STAGE 2
#
# -------------------------------------------------------------

   # The code above has loaded this code at absolute address 0x20000
   # now we have more than 512 bytes to execute.

stage2_entry:

   # Set all segments to match where this code is loaded
   mov ax, DEST_DATA_SEGMENT
   mov es, ax
   mov fs, ax
   mov gs, ax

   mov ah, 0x0 # set video mode
   mov al, 0x3 # 80x25 mode
   int 0x10

   # Hello message, just a "nice to have"
   mov si, offset str_hello
   call print_string

   mov si, offset str_device
   mov ax, [current_device]
   call print_string_and_num

   mov si, offset str_cylinders
   mov ax, [cylinders_count]
   call print_string_and_num

   mov si, offset str_heads_per_cyl
   mov ax, [heads_per_cylinder]
   call print_string_and_num

   mov si, offset str_sectors_per_track
   mov ax, [sectors_per_track]
   call print_string_and_num


   #########################################################################

   cli          # disable interrupts

   call smart_enable_A20

   jmp enter_unreal_mode

   saved_cx: .word 0
   saved_dx: .word 0

   actual_sectors_read: .long 0
   ramdisk_dest_addr32: .long RAMDISK_PADDR


   gdt:
   gdt_null: .byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
   gdt_code: .byte 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x9A, 0xCF, 0x00
   gdt_data: .byte 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x92, 0xCF, 0x00

   gdtr:
      .word 0x0023
      .long 0x00000000

   idtr:
      .word 0x0000
      .long 0x00000000

   small_buf: .space 8, 0

   newline:
      .asciz "\r\n"
   load_failed:
      .asciz "LBA: "
   cyl_param:
      .asciz "C: "
   head_param:
      .asciz "H: "
   sector_param:
      .asciz "S: "
   last_op_status:
      .asciz "Last op status: "
   str_device:
      .asciz "Device number: "
   str_cylinders:
      .asciz "Cyclinders count:   "
   str_heads_per_cyl:
      .asciz "Heads per cylinder: "
   str_sectors_per_track:
      .asciz "Sectors per track:  "
   str_hello:
      .asciz "Hello, I am the 2nd stage-bootloader!\r\n"
   err_while_loading_ramdisk:
      .asciz "Error while loading ramdisk\r\n"
   str_load_of_ramdisk_completed:
      .asciz "Loading of ramdisk completed.\r\n"
   str_before_reading_curr_sec:
      .asciz "Current sector num: "
   str_curr_sector_num:
      .asciz "After reading, current sector: "
   str_loading_ramdisk:
      .asciz "Loading ramdisk "


enter_unreal_mode:

   # calculate the absolute 32 bit address of GDT
   # since flat addr = SEG << 4 + OFF
   # that's exactly what we do below (SEG is DS)

   xor eax, eax
   mov ax, cs
   shl eax, 4
   add eax, offset gdt
   mov dword ptr [gdtr+2], eax

   lgdt [gdtr]            # load gdt register

   mov eax, cr0           # switch to 16-bit pmode by
   or al, 1               # set pmode bit
   mov cr0, eax

   jmp .+2                # tell 386/486 to not crash

   mov bx, 0x10           # select descriptor 2
   mov es, bx             # store it in 'es'.
                          # After that, only it can be used for indexing
                          # 32-bit addresses from "unreal mode".

   and al, 0xFE           # back to realmode
   mov cr0, eax           # by toggling bit again

   ###########################################################################

   sti # re-enable interrupts

   mov si, offset str_loading_ramdisk
   call print_string

   mov dword ptr [curr_sec], RAMDISK_FIRST_SECTOR

   .load_vdisk_loop:

      # Progress by showing dots: faster than full verbose strings

      mov eax, [curr_sec]
      and eax, 255
      test eax, eax
      jnz .after_print_dot

      .print_dot:
      mov ah, 0x0E # print char
      mov al, 46   # dot '.'
      int 0x10
      .after_print_dot:

      #mov eax, [curr_sec]
      #cmp eax, 53248
      #jne .cont

      #call lba_to_chs
      #mov [saved_cx], cx
      #mov [saved_dx], dx
      #mov si, offset newline
      #call print_string
      #call print_chs

      #cli
      #hlt
      #.cont:

      mov eax, [curr_sec]
      call lba_to_chs
      mov ax, TEMP_DATA_SEGMENT
      mov es, ax        # set the destination segment
      mov bx, 0         # set the destination offset

      # save the CHS parameters for error messages
      mov [saved_cx], cx
      mov [saved_dx], dx

      mov ah, 0x02                # Params for int 0x13: read sectors
      mov al, [sectors_per_track] # Read MAX possible sectors
      #mov al, 1
      int 0x13
      jc read_error

      mov [actual_sectors_read], al  # save the actual number of sectors read

      xor ax, ax
      mov es, ax # es = 0

      mov edi, [ramdisk_dest_addr32]          # dest flat addr
      mov esi, (TEMP_DATA_SEGMENT * 16)       # src flat addr

      mov edx, esi                     # edx = TEMP segment flat addr
      mov eax, [actual_sectors_read]   # eax = actual_sectors_read
      add [curr_sec], eax              # curr_sec += actual_sectors_read
      shl eax, 9                       # eax *= 512 (SECTOR_SIZE)
                                       # eax = total bytes read in this iter
      add [ramdisk_dest_addr32], eax   # ramdisk_dest_addr32 += tot_bytes
      add edx, eax                     # edx = TEMP_DATA_SEGMENT + tot_bytes

      .copy_loop:
         mov ebx, es:[esi]  # copy src data in ebx
         mov es:[edi], ebx  # copy ebx in dest ptr
         add edi, 4
         add esi, 4

         cmp esi, edx
         jle .copy_loop

      cmp dword ptr [curr_sec], RAMDISK_LAST_SECTOR
      jl .load_vdisk_loop

   # Loading of RAMDISK completed.

   mov si, offset newline
   call print_string

   mov si, offset str_load_of_ramdisk_completed
   call print_string

   ###########################################################################

enter_32bit_protected_mode:

   cli

   # now we have to copy the text from
   # complete_flush + 0x0 to complete_flush + 128
   # into 0x0000:COMPLETE_FLUSH_ADDR

   mov si, offset complete_flush
   mov di, COMPLETE_FLUSH_ADDR
   mov cx, COMPLETE_FLUSH_SIZE/2 # 2-byte words

   mov ax, 0
   mov es, ax # using extra segment for 0x0
   rep movsw  # copies 2*CX bytes from [ds:si] to [es:di]

   lidt [idtr]


   # layout of segments:
   # http://stackoverflow.com/questions/23978486/far-jump-in-gdt-in-bootloader

   # index (13b) | TI (2b) | RPL (2b)
   # TI = table indicator# 0 = GDT, 1 = LDT
   # RPL = Requestor priviledge level# 00 = highest, 11 = lowest

   lgdt [gdtr]  # load GDT register with start address of Global Descriptor Table

   # FIRST switch to protected mode and THEN do the FAR JUMP to 32 bit code

   mov eax, cr0
   or al, 1     # set PE (Protection Enable) bit in CR0 (Control Register 0)
   mov cr0, eax

   # 0x08 is the first selector
   # 0000000000001     0         00
   # index 1 (code)   GDT    privileged

   cli
   jmp 0x08:0x1000 # the JMP sets CS (code selector)#


##########################################


read_error:

   ################################################################
   # ERROR HANDLING CODE
   ################################################################

   mov si, offset err_while_loading_ramdisk
   call print_string

   # The load failed for some reason

   mov ah, 0x01 # Get Status of Last Drive Operation
   mov dl, [current_device]
   int 0x13

   # The carry flag here is always set and people on os-dev say
   # that one should not rely on it.

   # We have now in AH the last error
   shr ax, 8 # move AH in AL and make AH=0
   mov si, offset last_op_status
   call print_string_and_num


   # Print the sector number (LBA)
   mov si, offset load_failed
   mov ax, [curr_sec]
   call print_string_and_num

   # Print the CHS params we actually used
   call print_chs

   .hang:
   cli
   hlt

# Function: check_a20
#
# Purpose: to check the status of the a20 line in a completely self-contained state-preserving way.
#          The function can be modified as necessary by removing push's at the beginning and their
#          respective pop's at the end if complete self-containment is not required.
#
# Returns: 0 in ax if the a20 line is disabled (memory wraps around)
#          1 in ax if the a20 line is enabled (memory does not wrap around)
#
# Source: https://wiki.osdev.org/A20_Line
#

check_a20:
   pushf
   push ds
   push es
   push di
   push si

   cli

   xor ax, ax # ax = 0
   mov es, ax

   not ax # ax = 0xFFFF
   mov ds, ax

   mov di, 0x0500
   mov si, 0x0510

   mov al, es:[di]
   push ax

   mov al, ds:[si]
   push ax

   mov byte ptr es:[di], 0x00
   mov byte ptr ds:[si], 0xFF

   cmp byte ptr es:[di], 0xFF

   pop ax
   mov byte ptr ds:[si], al

   pop ax
   mov byte ptr es:[di], al

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

# Source: https://wiki.osdev.org/A20_Line
enable_A20_bios:
   mov ax, 0x2401
   int 0x15
   ret

# Source: https://wiki.osdev.org/A20_Line
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

# Source: https://wiki.osdev.org/A20_Line
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

   push offset small_buf
   push ax # the input number
   call itoa
   add sp, 4

   mov si, offset small_buf
   call print_string

   mov si, offset newline
   call print_string

   popa
   ret



# IN: string in SI
# IN: number in AX

print_string_and_num:
   call print_string
   call print_num
   ret

itoa: # convert 16-bit integer to string

#  USAGE:
#  push destbuffer
#  push number
#
#  call itoa
#  add sp, 4

   push bp
   mov bp, sp
   sub sp, 24

   mov [bp-2], bp
   sub word ptr [bp-2], 4

   .loop:

      xor dx, dx
      mov ax, [bp+4]
      mov bx, 10
      div bx
      mov [bp+4], ax

      mov bx, [bp-2]
      add dl, 48
      mov [bx], dl
      add word ptr [bp-2], 1

      mov cx, [bp+4]
      test cx, cx
      jne .loop   # JMP if cx != 0

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

   mov byte ptr [di], 0

   leave
   ret

print_chs:
   pusha

   # print the cylinder param
   mov si, offset cyl_param
   call print_string
   mov ax, [saved_cx]
   shr ax, 8     # put 'ch' in al

   mov bx, [saved_cx]
   and bx, 192   # make only the bits 6 and 7 to remain
   shl bx, 2
   or ax, bx
   call print_num

   # print the head param
   mov si, offset head_param
   call print_string
   mov ax, [saved_dx]
   shr ax, 8    # put 'dh' in al
   call print_num

   # print the sector param
   mov si, offset sector_param
   call print_string
   mov ax, [saved_cx]
   and ax, 63      # only the first 6 bits matter
   call print_num

   popa
   ret

#########################################################

.code32

complete_flush:


   # 0x10:
   # 0000000000010     0         00
   # index 2 (data)   GDT    privileged

   mov ax, 0x10
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax
   mov ss, ax

   # Set the stack for the 3rd stage of the bootloader
   mov esp, 0x0000FFF0

   # jump to the 3rd stage of the bootloader
   jmp 0x08:(DEST_DATA_SEGMENT * 16 + bootloader_3rd_stage)

.space COMPLETE_FLUSH_SIZE-(.-complete_flush), 0
.space VALUE_4K-(.-_start), 0


bootloader_3rd_stage:
# !!! DON'T ADD CODE HERE: this label is just a placeholder !!!
# !!! NOTE: The execution continues in stage3_entry.asm     !!!

.end
