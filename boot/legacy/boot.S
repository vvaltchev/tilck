# SPDX-License-Identifier: BSD-2-Clause

.intel_syntax noprefix

#include <tilck/common/config.h>

#define BASE_LOAD_SEG          0x07C0


/*
 * We're OK with just 48 KB, a little more than the size of the 3rd stage of
 * the bootloader + 4 KB for this code. All the rest (the whole ramdisk
 * and the ELF kernel) is loaded by the 3rd stage.
 */

#define INITIAL_SECTORS_TO_READ (48 * KB / SECTOR_SIZE)

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

   # relocate to BL_ST2_DATA_SEG

   mov ax, BASE_LOAD_SEG
   mov ds, ax
   mov ax, BL_ST2_DATA_SEG
   mov es, ax

   xor si, si # si = 0
   xor di, di # di = 0
   mov cx, 256 # 256 words = 512 bytes
   rep movsw

   jmp BL_ST2_DATA_SEG:after_reloc

after_reloc:

   xor ax, ax
   mov ss, ax      # Set stack segment and pointer
   mov sp, 0xFFF0
   sti             # Restore interrupts

   mov ax, BL_ST2_DATA_SEG # Update ds to match the new cs segment.
   mov ds, ax

   mov [current_device], dl # Save the current device

   mov si, offset str_loading
   call print_string

   xor ax, ax
   mov es, ax
   mov di, ax

   mov dl, [current_device]
   mov ah, 0x8 # read drive parameters
   int 0x13
   jc end

   # read drive params: OK

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
   shl cx, 2   # bugfix: before, the left shift was 8.
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
   #
   # NOTE: at the moment we don't need to load more then 64 KB here, but it's
   # better to just leave the code to support that, in case it is necessary
   # in the future.

   and ax, 0x7F
   test ax, ax
   jne .load_loop # JMP if ax != 0


   mov ax, [curr_data_seg]

   # Increment the segment by 4K => 64K in plain address space
   add ax, 0x1000
   mov [curr_data_seg], ax
   jmp .load_loop

.load_OK:
   jmp BL_ST2_DATA_SEG:stage2_entry

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

curr_data_seg:        .word BL_ST2_DATA_SEG
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
   mov ax, BL_ST2_DATA_SEG
   mov es, ax
   mov fs, ax
   mov gs, ax

   mov ah, 0x0 # set video mode
   mov al, 0x3 # 80x25 mode
   int 0x10

   # Hello message, just a "nice to have"
   mov si, offset str_hello
   call print_string

   call smart_enable_A20

enter_32bit_protected_mode:

   cli

   lidt [idtr]
   lgdt [gdtr_base_X]

   # enable protected mode
   mov eax, cr0
   or al, 1
   mov cr0, eax

   jmp 0x08:complete_flush

.code32

complete_flush:

   lgdt [gdtr_flat]
   jmp 0x08:(BL_ST2_DATA_SEG * 16 + complete_flush2)

complete_flush2:

   mov ax, 0x10
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax
   mov ss, ax

   # Move in dx the current device
   mov dx, [BL_ST2_DATA_SEG * 16 + current_device]

   # jump to the 3rd stage of the bootloader
   jmp 0x08:BL_BASE_ADDR

#########################################################

gdt_base_X:
.byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 # NULL segment

# code base = BL_ST2_DATA_SEG*16
.byte 0xFF, 0xFF # limit

# base (first 24-bit bits)
.byte ((BL_ST2_DATA_SEG * 16) & 0xff)
.byte (((BL_ST2_DATA_SEG * 16) >> 8) & 0xff)
.byte (((BL_ST2_DATA_SEG * 16) >> 16) & 0xff)

.byte 0x9A, 0xCF, 0x00 # access, granularity and high base

# data base = BL_ST2_DATA_SEG*16
.byte 0xFF, 0xFF # limit

# base (first 24-bit bits)
.byte ((BL_ST2_DATA_SEG * 16) & 0xff)
.byte (((BL_ST2_DATA_SEG * 16) >> 8) & 0xff)
.byte (((BL_ST2_DATA_SEG * 16) >> 16) & 0xff)

.byte 0x92, 0xCF, 0x00 # access, granularity and high base

gdt32_flat:
.byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 # NULL segment
.byte 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x9A, 0xCF, 0x00 # code base = 0
.byte 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x92, 0xCF, 0x00 # data base = 0

gdtr_base_X:
   .word 0x23
   .long (BL_ST2_DATA_SEG * 16 + offset gdt_base_X)

gdtr_flat:
   .word 0x23
   .long (BL_ST2_DATA_SEG * 16 + offset gdt32_flat)

idtr:
   .word 0
   .long 0

str_hello:
   .asciz "Hello, I am the 2nd stage-bootloader!\r\n"


