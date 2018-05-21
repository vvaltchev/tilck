
.intel_syntax noprefix

.section .text
.global _start
.global main
.global go_to_real_mode_and_back

#define BASE_ADDR 0x21000
#define RELOC_ADDR 0x2000

_start:
main:

1:
   # Copy this code to RELOC_ADDR in order to allow switch back to realmode
   mov esi, offset _start
   mov edi, RELOC_ADDR
   mov ecx, 0x400  # 1024
   rep movsd

   # Zero-out the BSS
   mov ecx, offset __bss_end
   sub ecx, offset __bss_start
   xor eax, eax
   rep stosb

   # Jump to bootloader's main
   jmp bootloader_main

########################################

go_to_real_mode_and_back:

   pusha
   mov [saved_pm32_esp], esp

   # load the new gdt, with 16-bit entries
   lea eax, [gdtr+2]
   mov dword ptr [eax], offset gdt16
   lgdt [gdtr]

   # Load ds, es, fs, gs, ss with a 16-bit data segment.
   mov ax, 0x10
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax
   mov ss, ax

   # Jump to a CODE16 segment
   jmp 0x08:(code16_seg - BASE_ADDR + RELOC_ADDR)

code16_seg:

   mov eax, cr0
   and al, 0xfe # Disable protected mode.
   mov cr0, eax

.code16
   mov ebx, (offset realmode - BASE_ADDR + RELOC_ADDR)
   push 0x2100
   push ebx
   retf # jumps to realmode

realmode:

   mov ax, 0x00
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax
   mov ss, ax

   mov sp, 0xFFF0

   mov     edi, (offset idt16 - BASE_ADDR + RELOC_ADDR)
   lidt    [di]

   sti

   mov ah, 0x00 # set video mode
   mov al, 0x03 # 80x25
   int 0x10     #

   mov ah, 0x0E    # int 10h 'print char' function

   mov al, 'B'
   int 0x10
   mov al, 'O'
   int 0x10
   mov al, 'O'
   int 0x10
   mov al, 'T'
   int 0x10
   mov al, 'E'
   int 0x10
   mov al, 'D'
   int 0x10
   mov al, 13 # \r
   int 0x10
   mov al, 10 # \n
   int 0x10

   # now we have to go back to protected mode

   cli

   # load the 32-bit NULL IDT
   mov edi, (offset idt32 - BASE_ADDR + RELOC_ADDR)
   lidt [di]

   # load the a gdt with 32-bit segments
   mov ebx, (offset gdtr - BASE_ADDR + RELOC_ADDR)
   lea eax, [ebx + 2]
   mov dword ptr [eax], (offset gdt32 - BASE_ADDR + RELOC_ADDR)
   lgdt [ebx]

   mov eax, cr0
   or al, 1 # PE bit
   mov cr0, eax

   mov eax, (offset pm32 - BASE_ADDR + RELOC_ADDR)
   push 0x08
   push ax
   retf

.code32

pm32:
   mov ax, 0x10
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax
   mov ss, ax

   mov esp, [saved_pm32_esp]
   popa
   ret

# Data
#################################

saved_pm32_esp:
   .long 0

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
   .word 0x0000 # base first 0-15 bits
   .byte 0x00   # base 16-23 bits
   .byte 0x9A   # access byte
                # | present | priv (2) | 1 (reserved) | exec | dc | rw | ac

   .byte 0x00   # high 4 bits (flags); low 4 bits (limit 4 higher bits)
                # granularity | size (16/32) | 0 | 0 | limit HI (4 bits)

   .byte 0x00   # base 24-31 bits

   # sel 0x10. 64k 16-bit data
   .byte 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x92, 0x00, 0x00

gdtr:
   .word 0x23
   .long 0

.space (4096)-(.-_start), 0
