
.intel_syntax noprefix

#include <common/config.h>

# Usable (un)real mode functions

.code16

.global realmode_test_out
realmode_test_out:

   mov ax, 23
   mov bx, 99
   mov cx, 100
   mov dx, 102
   mov si, 300
   mov di, 350
   ret

.global realmode_int_10h
realmode_int_10h:

   int 0x10
   ret

.global realmode_int_13h
realmode_int_13h:

   int 0x13
   ret

.global realmode_int_16h
realmode_int_16h:

   int 0x16
   ret

#############################################################################

lba_to_chs:         # Calculate head, track and sector settings for int 13h
                    # IN:  LBA in EAX
                    # OUT: correct registers for int 13h
   push ebx
   push edi

   .byte 0x66
   push eax        # Save LBA

   # DIV {ARG}
   # divides DX:AX by {ARG}
   # quotient => AX
   # reminder => DX

   mov ebx, (offset sectors_per_track - BL_BASE_ADDR)
   mov ebx, [bx]

   xor edx, edx      # First the sector
   div ebx           # ax = LBA / sectors_per_track
   inc edx           # Physical sectors start at 1
   mov cl, dl        # Sectors belong in CL for int 13h
   and cl, 63        # Make sure the upper two bits of CL are unset

   .byte 0x66
   pop eax           # reload the LBA sector in AX

   xor edx, edx      # reset DX and calculate the head
   div ebx           # ax = LBA / sectors_per_track

   mov ebx, (offset heads_per_cylinder - BL_BASE_ADDR)
   mov ebx, [bx]
   xor edx, edx
   div ebx           # ax = (LBA / sectors_per_track) / heads_per_cylinder
   mov dh, dl        # Head
   mov ch, al        # Cylinder

   shr ax, 2         # Move the 2 higher bits of cylinder down
   and ax, 192       # make only bits 6 and 7 to remain
   or cl, al         # OR those two bits in CL, since its 2 upper-bits
                     # are the upper bits of cylinder.

   mov edi, (offset current_device - BL_BASE_ADDR)
   mov dl, [di]      # Set correct device

   pop edi
   pop ebx

   ret


#define TEMP_DATA_SEGMENT           0x1000

#
# Input:
#
# eax => dest paddr
# ebx => first logical sector
# ecx => last logical sector (inclusive)
#

.global realmode_read_sectors
realmode_read_sectors:

   mov edi, (offset ramdisk_dest_addr32 - BL_BASE_ADDR)
   mov [di], eax

   mov edi, (offset curr_sec - BL_BASE_ADDR)
   mov [di], ebx

   mov edi, (offset last_sec - BL_BASE_ADDR)
   mov [di], ecx

   push es
   mov ax, TEMP_DATA_SEGMENT
   mov es, ax        # set the destination segment

   .load_vdisk_loop:

      mov ebx, (offset curr_sec - BL_BASE_ADDR)
      mov eax, [bx]

      mov ebx, (offset last_sec - BL_BASE_ADDR)
      mov ebx, [bx]
      inc ebx
      sub ebx, eax # ebx = remaining sectors to read

      mov edi, (offset sectors_per_track - BL_BASE_ADDR)
      mov ecx, [di] # ecx = max sectors per read

      cmp ebx, ecx  # compare 'remaining sectors' with 'max sectors'
      jge .many_sectors_remaining

      # A few sectors remaining
      # NOTE: In 'bl' we already have the number of sectors value we need
      jmp .call_lba_to_chs

.many_sectors_remaining:
      mov bx, cx       # set sectors to read to MAX (=> sectors per track)

.call_lba_to_chs:

      push bx
      call lba_to_chs
      pop bx

      # save the CHS parameters for error messages
      mov edi, (offset saved_cx - BL_BASE_ADDR)
      mov esi, (offset saved_dx - BL_BASE_ADDR)
      mov [di], cx
      mov [si], dx

      mov ah, 0x02      # Params for int 0x13: read sectors in ES:BX
      mov al, bl        # Set number of sectors to read
      mov bx, 0         # set the destination offset
      int 0x13
      jc read_error

      mov edi, (offset actual_sectors_read - BL_BASE_ADDR)
      mov [di], al  # save the actual number of sectors read

      mov edi, (offset ramdisk_dest_addr32 - BL_BASE_ADDR)
      mov edi, [di]                           # dest flat addr
      mov esi, (TEMP_DATA_SEGMENT * 16)       # src flat addr

      mov edx, esi                     # edx = TEMP segment flat addr
      mov ebx, (offset actual_sectors_read - BL_BASE_ADDR)
      mov eax, [bx]                    # eax = actual_sectors_read

      mov ebx, (offset curr_sec - BL_BASE_ADDR)
      add [bx], eax                    # curr_sec += actual_sectors_read
      shl eax, 9                       # eax *= 512 (SECTOR_SIZE)
                                       # eax = total bytes read in this iter

      mov ebx, (offset ramdisk_dest_addr32 - BL_BASE_ADDR)
      add [bx], eax                    # ramdisk_dest_addr32 += tot_bytes
      add edx, eax                     # edx = TEMP_DATA_SEGMENT + tot_bytes

      .copy_loop:
         mov ebx, fs:[esi]  # copy src data in ebx
         mov fs:[edi], ebx  # copy ebx in dest ptr
         add edi, 4
         add esi, 4

         cmp esi, edx
         jle .copy_loop

      mov ebx, (offset curr_sec - BL_BASE_ADDR)
      mov ebx, [bx]

      mov edi, (offset last_sec - BL_BASE_ADDR)
      mov ecx, [di]

      cmp ebx, ecx
      jl .load_vdisk_loop

   # Loading of RAMDISK completed.
   mov eax, 0
   jmp end

read_error:
   mov edi, (offset current_device - BL_BASE_ADDR)
   mov ah, 0x01 # Get Status of Last Drive Operation
   mov dl, [di]
   int 0x13
   # We have now in AH the last error

end:
   pop es
   ret

#####################################
# Data

saved_cx: .word 0
saved_dx: .word 0

curr_sec:             .long 0
last_sec:             .long 0
actual_sectors_read:  .long 0
ramdisk_dest_addr32:  .long 0
