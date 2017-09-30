[BITS 64]
[ORG 0xC000]

%define KERNEL_PADDR 0x100000 ; + 1 MB

; DEBUGGING with GDB
; in order to debug mode switches, we need manually to tell GDB the right mode
;
; force 64 bit
; set arch i386:x86-64:intel
;
; force 32 bit
; set arch i386
;
; see current architecture
; show architecture
;

start:

   cli

   lea eax, [gdtr+2]
   mov dword [eax], gdt32
   lgdt [gdtr]

   jmp far [far_ptr]

far_ptr:
   dq CompatibilityMode64_32
   dw 0x08 ; cs


[BITS 32]

   ; here we are in a compatibility 32 bit mode of long mode
   ; so, we're using 32 bit, but we're still (kind of) in long mode.
   ; here in GDB, change the arch with:
   ; > set arch i386
   ; for further debugging.

CompatibilityMode64_32:

   mov ax, 0x10
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax
   mov ss, ax
   mov esp, 0xFFF0

   ; Disable Paging to get out of Long Mode
   mov eax, cr0 ; Read CR0.
   and eax, 0x7fffffff ; disable paging
   mov cr0, eax ; Write CR0.

   ; Deactivate Long Mode
   mov ecx, 0xC0000080 ; EFER MSR number.
   rdmsr ; Read EFER.
   btc eax, 8 ; Set LME=0.
   wrmsr ; Write EFER.

   mov eax, cr4
   and eax, 0xFFFFFFDF ; disable PAE
   mov cr4, eax

   xor eax, eax
   mov cr3, eax ; it's a good practice to reset cr3, as well


   ; load the new gdt, with 16-bit entries

   lea eax, [gdtr+2]
   mov dword [eax], gdt16
   lgdt [gdtr]


   ; Load ds, es, fs, gs, ss with a 16-bit data segment.

   mov ax, 0x10
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax
   mov ss, ax


   ; Jump back to a CODE16 segment
   jmp 0x08:code16_seg

code16_seg:

   mov     eax, cr0
   and     al, 0xfe ; Disable protected mode.
   mov     cr0, eax

[BITS 16]
   jmp 0x00:realmode

   nop
   nop
   nop
   nop

realmode:

   mov ax, 0x0000
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax

   mov ss, ax      ; Set stack segment and pointer
   mov sp, 0xFFF0

   mov     di, idt16
   lidt    [di]

   sti

   mov ah, 0x00 ; set video mode
   mov al, 0x03 ; 80x25
   int 0x10     ;

   mov ah, 0x0E    ; int 10h 'print char' function

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
   mov al, 13 ; \r
   int 0x10
   mov al, 10 ; \n
   int 0x10

   ; now we have to go back to protected mode

   cli

   ; load the 32-bit NULL IDT
   lidt [idt32]

   ; load the a gdt with 32-bit segments
   lea eax, [gdtr+2]
   mov dword [eax], gdt32
   lgdt [gdtr]

   mov eax, cr0
   or al, 1 ; PE bit
   mov cr0, eax

   jmp 0x08:pm32

[BITS 32]

pm32:

   mov ax, 0x10
   mov ds, ax
   mov es, ax
   mov fs, ax
   mov gs, ax
   mov ss, ax

   ; jump to kernel
   jmp 0x08:KERNEL_PADDR

   ; --- we should hever get past this point ---

idt16:
   dw 0x03ff          ; 256 entries, 4b each = 1K
   dd 0               ; Real Mode IVT @ 0x0000

idt32:
   dw 0
   dd 0

; GDT selectors:
;
; [bits 15 .. 3] | [bit 2]  | [bits 1..0]
;   GDT index    | GDT/LDT  |    RPL
;
; GDT/LDT: GDT=0, LDT=1
; RPL: requested priv. level
; 00 = ring 0
; 01 = ring 1
; 10 = ring 2
; 11 = ring 3

; for gdt:
; http://wiki.osdev.org/Global_Descriptor_Table

gdt32:
   db 0x00, 0x00, 0, 0, 0, 0x00, 0x00, 0  ; sel 0x00.
   db 0xFF, 0xFF, 0, 0, 0, 0x9A, 0xCF, 0  ; sel 0x08. 32-bit code
   db 0xFF, 0xFF, 0, 0, 0, 0x92, 0xCF, 0  ; sel 0x10. 32-bit data

gdt16:
   db 0x00, 0x00, 0, 0, 0, 0x00, 0x00, 0  ; sel 0x00.

   ; sel 0x08. 64k 16-bit code
   dw 0xFFFF ; segment limit first 0-15 bits
   dw 0x0000 ; base first 0-15 bits
   db 0x00   ; base 16-23 bits
   db 0x9A   ; access byte
             ; | present | priv (2) | 1 (reserved) | exec | dc | rw | ac

   db 00000000b      ; high 4 bits (flags); low 4 bits (limit 4 higher bits)
                     ; granularity | size (16/32) | 0 | 0 | limit HI (4 bits)

   db 0x00           ; base 24-31 bits

   db 0xFF, 0xFF, 0, 0, 0, 0x92, 00000000b, 0  ; sel 0x10. 64k 16-bit data


; GDTR format
; size-1 [ 2 bytes ], gdt addr [ 4 bytes ]

gdtr:
   dw 0x0023
   dd 0x00000000


