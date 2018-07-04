
.intel_syntax noprefix

#include <exos/common/config.h>

.section .text
.global _start
.global main
.global realmode_call_asm

_start:
main:

   # Clear the direction flag since the C compiler has the right
   # to assume that is set to 0 (forward).
   cld

   # Set the stack for the 3rd stage of the bootloader
   mov esp, 0x0003FFF0

   # Zero the BSS
   mov edi, offset __bss_start
   mov ecx, offset __bss_end
   sub ecx, edi
   xor eax, eax
   rep stosb

   # Save current device's number
   mov [current_device], dx

   call setup_realmode_ivt

   # Jump to bootloader's main
   jmp bootloader_main


# Actually, in our case it's "unreal" mode
realmode_call_asm:

   pusha
   mov [saved_pm32_esp], esp

   mov eax, [esp + 36]               # arg1 : func to call
   mov [realmode_func_addr], eax

   mov ecx, 7
   lea esi, [esp + 40]
   mov edi, offset realmode_func_eax_ref
   rep movsd

   # load the new gdt, with 16-bit entries + 32-bit data (entry 0x10)
   lgdt [gdtr16]

   # Jump to a CODE16 segment

   # Note: we haven't touched the 'fs' segment register.
   # It will remain "cached" as a 32-bit selector (we want UNREAL mode).

   mov ax, 0x10
   mov ds, ax
   mov ss, ax
   mov es, ax
   mov gs, ax

   # Note(2): we're jumping to 'code16_seg' with addressing relative to 0,
   # because in the gdt16, the code segment has BL_BASE_ADDR as 'base'.
   jmp 0x08:(code16_seg - BL_BASE_ADDR)

code16_seg:

   # If we set all the segment registers here we would be properly in
   # 16-bit protected mode.

   mov eax, cr0
   and al, 0xfe # Disable protected mode.
   mov cr0, eax

.code16
   # here CS is still 0x08, time to switch to proper 16-bit segmentation.
   mov ebx, (offset realmode - BL_BASE_ADDR)
   push BL_BASE_SEG
   push bx
   retf

realmode:
   # here CS is BL_BASE_SEG and we are properly in (un)real mode.

   mov ax, 0
   mov es, ax
   mov fs, ax
   mov gs, ax

   # Now we just need to set ds, ss to BL_BASE_SEG, using the 16-bit
   # segmentation mechanism. This makes everything simpler.

   mov ax, BL_BASE_SEG
   mov ds, ax
   mov ss, ax
   mov esp, (offset rm16_stack - BL_BASE_ADDR + 0x3F0)

   mov     edi, (offset idt16 - BL_BASE_ADDR)
   lidt    [di]

   sti

   #################################################################
   # Call the requested function

   # 1. store in 'ax' the address of the function to call
   mov eax, (offset realmode_func_addr)
   mov eax, fs:[eax]
   sub eax, BL_BASE_ADDR

   # 2. save on stack the return address
   mov ebx, (offset after_call - BL_BASE_ADDR)
   push bx

   # 3. push func's addr on stack in order the ending ret to make the "call"
   push ax

   # 4. now that we're ready to call the function, we can load the registers,
   # as specified from the C caller. Note: we couldn't just do a call at the
   # end instead: that would require using at least a register for computing
   # the address and we don't want that. The C caller should be able to set
   # all of the 6 registers: eax, ebx, ecx, edx, esi, edi.

   mov eax, (offset realmode_func_eax_ref)
   mov eax, fs:[eax]
   mov eax, fs:[eax]

   mov ebx, (offset realmode_func_ebx_ref)
   mov ebx, fs:[ebx]
   mov ebx, fs:[ebx]

   mov ecx, (offset realmode_func_ecx_ref)
   mov ecx, fs:[ecx]
   mov ecx, fs:[ecx]

   mov edx, (offset realmode_func_edx_ref)
   mov edx, fs:[edx]
   mov edx, fs:[edx]

   mov esi, (offset realmode_func_esi_ref)
   mov esi, fs:[esi]
   mov esi, fs:[esi]

   mov edi, (offset realmode_func_edi_ref)
   mov edi, fs:[edi]
   mov edi, fs:[edi]

   # Call the function pointed at realmode_func_addr
   ret

after_call:

   # now here we must save back the values of the registers

   mov bp, sp
   mov esp, (offset rm_ret_eax - BL_BASE_ADDR + 4)

   push eax
   push ebx
   push ecx
   push edx
   push esi
   push edi

   sub sp, 2
   pushf

   mov sp, bp

   ##################################################################
   # now we have to go back to protected mode

   cli

   # load the 32-bit NULL IDT
   mov edi, (offset idt32 - BL_BASE_ADDR)
   lidt [di]

   # load the a gdt with 32-bit segments
   mov ebx, (offset gdtr32 - BL_BASE_ADDR)
   lgdt [ebx]

   mov eax, cr0
   or al, 1 # PE bit
   mov cr0, eax


   # Use 0x66 (Operand Size Override Prefix) before push and retf to use the
   # 32-bit version of those instructions. That is NECESSARY if we want to do
   # a 32-bit far jump. Otherwise, the pm32 code has to be copied to a low
   # address (< 64 K), in order to the 16-bit far jump to able able to get
   # there.

   mov eax, (offset pm32)
   push 0x08
   .byte 0x66
   push eax
   .byte 0x66
   retf


.code32

pm32:
   mov ax, 0x10
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax
   mov ss, ax

   xor eax, eax

   # Copy rm_ret_* at the address pointed by each realmode_func_*_ref

   lea esi, [rm_ret_eax]
   lea edi, [realmode_func_eax_ref]
   mov ecx, 7

1:
   mov eax, [esi]
   mov ebx, [edi]
   mov [ebx], eax
   sub esi, 4
   add edi, 4
   loop 1b

   mov esp, [saved_pm32_esp]
   popa
   ret

# NOTE: this macro assumes that the higher 16 bits of EAX contain the CS segment
.macro set_ivt num
   mov ebx, (offset ex\num - BL_BASE_ADDR)
   mov ax, bx
   mov [4 * \num], eax
.endm

setup_realmode_ivt:

   mov eax, (BL_BASE_SEG << 16)

   set_ivt  0
   # skip   1
   set_ivt  2
   set_ivt  3
   set_ivt  4
   set_ivt  5
   set_ivt  6
   set_ivt  7
   # skip   8
   # skip   9
   set_ivt 10
   set_ivt 11
   set_ivt 12
   set_ivt 13
   set_ivt 14

   ret

.code16

.macro emit_rm_ex_handler num
   ex\num:
   mov ecx, \num
   jmp common_rm_exception_handler
.endm

emit_rm_ex_handler  0
# skip              1
emit_rm_ex_handler  2
emit_rm_ex_handler  3
emit_rm_ex_handler  4
emit_rm_ex_handler  5
emit_rm_ex_handler  6
emit_rm_ex_handler  7
# skip              8
# skip              9
emit_rm_ex_handler 10
emit_rm_ex_handler 11
emit_rm_ex_handler 12
emit_rm_ex_handler 13
emit_rm_ex_handler 14
# skip             15
# the rest (0x10 and so on) are part of the BIOS interface

common_rm_exception_handler:
   mov eax, 0xBAADCAFE
   mov ebx, 0xCAFEBABE
   mov edx, [esp]       # contains (cs << 16 | ip)
   mov esi, [esp+4]
   jmp after_call


#################################
# Data

saved_pm32_esp:     .long 0
realmode_func_addr: .long 0

realmode_func_eax_ref:    .long 0
realmode_func_ebx_ref:    .long 0
realmode_func_ecx_ref:    .long 0
realmode_func_edx_ref:    .long 0
realmode_func_esi_ref:    .long 0
realmode_func_edi_ref:    .long 0
realmode_func_flags_ref:  .long 0

rm_ret_flags:   .long 0
rm_ret_edi:     .long 0
rm_ret_esi:     .long 0
rm_ret_edx:     .long 0
rm_ret_ecx:     .long 0
rm_ret_ebx:     .long 0
rm_ret_eax:     .long 0


idt16:
   .word 0x03ff          # 256 entries, 4b each = 1K
   .long 0               # Real Mode IVT @ 0x0000

idt32:
   .word 0
   .long 0

gdt32:
   .byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 # sel 0x00.
   .byte 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x9A, 0xCF, 0x00 # sel 0x08. 32-bit code
   .byte 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x92, 0xCF, 0x00 # sel 0x10. 32-bit data

gdt16:
   .byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  # sel 0x00.

   # sel 0x08. 64k 16-bit code
   .word 0xFFFF # segment limit first 0-15 bits

   # base (first 24-bit bits)
   .byte (BL_BASE_ADDR & 0xff)
   .byte ((BL_BASE_ADDR >> 8) & 0xff)
   .byte ((BL_BASE_ADDR >> 16) & 0xff)

   .byte 0x9A   # access byte
                # | present | priv (2) | 1 (reserved) | exec | dc | rw | ac

   .byte 0x00   # high 4 bits (flags); low 4 bits (limit 4 higher bits)
                # granularity | size (16/32) | 0 | 0 | limit HI (4 bits)

   .byte 0x00   # base 24-31 bits

   # sel 0x10. 64k 16-bit data (unused)
   .byte 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x92, 0x00, 0x00


gdtr16:
   .word 0x23
   .long gdt16

gdtr32:
   .word 0x23
   .long gdt32

rm16_stack:
   .space 1024, 0

.space (4096)-(.-_start), 0
