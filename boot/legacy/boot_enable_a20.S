# SPDX-License-Identifier: CC0-1.0

.intel_syntax noprefix

.section .text

.global smart_enable_A20
.code16


# Function: check_a20
#
# Purpose:
#   to check the status of the a20 line in a completely self-contained
#   state-preserving way. The function can be modified as necessary by removing
#   push's at the beginning and their respective pop's at the end if complete
#   self-containment is not required.
#
# Returns:
#   0 in ax if the a20 line is disabled (memory wraps around)
#   1 in ax if the a20 line is enabled (memory does not wrap around)
#
# Source: https://wiki.osdev.org/A20_Line

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
