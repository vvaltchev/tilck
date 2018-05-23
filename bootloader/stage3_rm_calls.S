
.intel_syntax noprefix

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

.global realmode_int_16h
realmode_int_16h:

   int 0x16
   ret
