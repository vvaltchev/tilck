
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


.global realmode_func_set_video_mode
realmode_func_set_video_mode:

   mov ah, 0x00 # set video mode
   mov al, 0x03 # 80x25
   int 0x10
   ret

.global realmode_write_char
realmode_write_char:

   mov ah, 0x0E
   int 0x10
   ret
