/* SPDX-License-Identifier: BSD-2-Clause */

.intel_syntax noprefix

#include <tilck_gen_headers/config_global.h>
#include <tilck_gen_headers/config_boot.h>

#define BASE_LOAD_SEG          0x07C0 /* x86 BIOS constant, do *not* touch */

.org 0x0000

.global _start

.section .text

.code16

_start:

jmp reloc

/* Fill the gap with nops since bios_parameter_pack has to be at offset +0x0B */
.space 0x0B - (.-_start)

bios_parameter_pack:
.space 32 /* Leave just enough space for a DOS EBPB 3.2 */

reloc:

   cli               # Disable interrupts
   cld               # The default direction for string operations
                     # will be 'up' - incrementing address in RAM

   # relocate to BL_ST2_DATA_SEG

   mov ax, BASE_LOAD_SEG
   mov ds, ax
   mov ax, BL_ST2_DATA_SEG
   mov es, ax

   xor si, si  # si = 0
   xor di, di  # di = 0
   mov cx, 256 # 256 words = 512 bytes
   rep movsw

   jmp BL_ST2_DATA_SEG:after_reloc

after_reloc:

   xor ax, ax
   mov ss, ax      # Set stack segment
   mov sp, 0xFFF0  # Set stack pointer
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
   jc error_end

   # read drive params: OK

   xor ax, ax
   mov al, dh
   inc al      # DH contains MAX head num, so we have to add +1.
   mov [heads_per_cylinder], ax

   mov ax, cx
   and ax, 63   # last 6 bits
   mov [sectors_per_track], ax # Actual number of sectors, NOT number_of - 1.
                               # Reason: CHS sectors start from 1, not zero.
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
   jc error_end

   mov ax, [curr_sec]

   # We read all the sectors we needed: loading is over.
   cmp ax, BOOT_SECTORS
   je .load_OK

   inc ax                    # we read just 1 sector at time
   mov [curr_sec], ax


   # If the current sector num have the bits 0-7 unset,
   # we've loaded 128 sectors * 512 bytes = 64K.
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

error_end:
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


.space 436 - (.-_start), 0x90    # Pad for MBR Partition Table

.byte 0x00, 0x00, 0x00, 0x00     # Unknown
.long DISK_UUID                  # Unique Disk ID (UUID)
.byte 0x00, 0x00                 # Reserved. 0x5A, 0x5A if read-only

PT1: .space 16, 0x00             # 1st Partition Entry
PT2: .space 16, 0x00             # 2nd Partition Entry
PT3: .space 16, 0x00             # 3rd Partition Entry
PT4: .space 16, 0x00             # 4th Partition Entry

.space 510-(.-_start), 0         # Pad remainder of boot sector with 0s
.word 0xAA55                     # Standard PC boot signature



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
   lgdt [gdtr_flat]

   # enable protected mode
   mov eax, cr0
   or al, 1
   mov cr0, eax

   # Use 0x66 (Operand Size Override Prefix) before push and retf to use the
   # 32-bit version of those instructions. That is NECESSARY if we want to do
   # a 32-bit far jump. Otherwise, the complete_flush code has to be copied to
   # a low address (< 64 K), in order to the 16-bit far jump to able able to get
   # there. Or, in alternative, we'd need to setup first a non-flat GDT with
   # BASE = BL_ST2_DATA_SEG * 16 for all the segments, then we'd need to load
   # the "flat GDT" with BASE = 0 and perform another far RET.

   mov eax, (offset BL_ST2_DATA_SEG * 16 + complete_flush)

   push 0x08

   .byte 0x66
   push eax

   .byte 0x66
   retf

.code32

complete_flush:

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

gdt32_flat:
.byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 # NULL segment
.byte 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x9A, 0xCF, 0x00 # code base = 0
.byte 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x92, 0xCF, 0x00 # data base = 0

gdtr_flat:
   .word 0x23
   .long (BL_ST2_DATA_SEG * 16 + offset gdt32_flat)

idtr:
   .word 0
   .long 0

str_hello:
   .asciz "Hello, I am the 2nd stage-bootloader!\r\n"


