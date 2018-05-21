
.intel_syntax noprefix

.section .text
.global _start
.global main
.global call_realmode_func

#define BASE_ADDR 0x21000
#define BASE_SEG (BASE_ADDR / 16)

#define RELOC_ADDR 0x2000
#define REALMODE_STACK 0xFFF0

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

call_realmode_func:

   pusha
   mov [saved_pm32_esp], esp

   mov eax, [esp+36]                 # arg1 : func to call
   sub eax, (BASE_ADDR - RELOC_ADDR) # adjust the address
   mov [realmode_func_addr], eax

   # load the new gdt, with 16-bit entries
   lgdt [gdtr16]

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
   push BASE_SEG
   push ebx
   retf # jumps to realmode

realmode:
   mov ax, 0x00
   mov ds, ax
   mov es, ax
   mov gs, ax
   mov ss, ax

   mov sp, REALMODE_STACK

   mov ax, BASE_SEG
   mov fs, ax

   mov     edi, (offset idt16 - BASE_ADDR + RELOC_ADDR)
   lidt    [di]

   sti
   #################################################################

   mov esi, (offset realmode_func_addr - BASE_ADDR)
   call fs:[si]

   ##################################################################
   # now we have to go back to protected mode
   cli

   # load the 32-bit NULL IDT
   mov edi, (offset idt32 - BASE_ADDR + RELOC_ADDR)
   lidt [di]

   # load the a gdt with 32-bit segments
   mov ebx, (offset gdtr32 - BASE_ADDR + RELOC_ADDR)
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

.code16

.global realmode_func_set_video_mode
realmode_func_set_video_mode:
   mov ah, 0x00 # set video mode
   mov al, 0x03 # 80x25
   int 0x10     #
   ret

.global realmode_func_hello
realmode_func_hello:

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

   ret

# Data
#################################

saved_pm32_esp: .long 0
realmode_func_addr: .long 0

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

gdtr16:
   .word 0x23
   .long gdt16

gdtr32:
   .word 0x23
   .long gdt32

.space (4096)-(.-_start), 0
