   
[BITS 16]

   ;;;;;;;;;;;;;;;;;
   ; EXAMPLE: print a char using BIOS
   ;
   ; mov ah, 0Eh
   ; mov al, 'k'

   ; int 10h
   ;;;;;;;;;;;;;;;;;
   
print_string:      ; Routine: output string in SI to screen
   push bp
   mov bp, sp
   
   mov si, [bp+4]  ; bp+4 is the first argument
   mov ah, 0Eh     ; int 10h 'print char' function

.repeat:
   lodsb           ; Get character from string
   cmp al, 0
   je .done        ; If char is zero, end of string
   int 10h         ; Otherwise, print it
   jmp .repeat

.done:
   leave
   ret

   
   
dump_binary: ; dump binary data to the screen in dec bytes

   ; USAGE:
   ; push buffer (16 bit ptr)
   ; push length (16 bit unsigned)
   ;
   ; call dump_binary
   ; add sp, 4

   push bp
   mov bp, sp
   sub sp, 24
   
   ; clear the buffer at [bp-24]
   mov word [bp-24], 0
   mov word [bp-22], 0
   mov word [bp-20], 0
   mov word [bp-18], 0
   mov word [bp-16], 0
   mov word [bp-14], 0
   mov word [bp-12], 0
   mov word [bp-10], 0
   
   
   .read_loop:
   
   mov ax, [bp+6] ; remaining length
   cmp ax, 0
   je .end  
   dec ax
   mov [bp+6], ax ; decrement length
   
   
   lea bx, [bp-24]
   push bx             ; dest buffer

   mov bx, [bp+4]      ; pointer to the binary data
   xor ax, ax
   mov al, [bx]        ; take just the LO byte  
   push ax             ; number: the char to be converted
   
   inc bx
   mov [bp+4], bx ; move ahead the pointer
  
   call itoa
   add sp, 4
   
   ; now in [bp-24] we have the string-repr of the number
   lea ax, [bp-24]
   push ax
   call print_string
   add sp, 2
   
   mov ah, 0Eh     ; int 10h 'print char' function
   mov al, 32      ; space (' ')
   int 10h
   
   jmp .read_loop
   
   .end:
   
   ; print new line
   
   mov ah, 0Eh     ; int 10h 'print char' function
   mov al, 13
   int 10h
   mov ah, 0Eh     ; int 10h 'print char' function
   mov al, 10
   int 10h
   
   leave
   ret

itoa: ; convert integer to string

;  USAGE:
;  push [in/out] destbuffer
;  push number
;
;  call itoa
;  add sp, 4

   push bp
   mov bp, sp
   sub sp, 24

   mov [bp-2], bp
   sub word [bp-2], 4

   .loop:

   mov dx, 0
   mov ax, [bp+4]
   mov bx, 10
   div bx
   mov [bp+4], ax

   mov bx, [bp-2]
   add dl, 48
   mov [bx], dl
   add word [bp-2], 1

   mov cx, [bp+4]
   cmp cx, 0
   jne .loop

   mov bx, [bp-2]
   sub bx, 1


   mov di, [bp+6]

   .l2:

   mov ax, [bx]
   mov [di], ax

   dec bx
   inc di

   mov ax, bp
   sub ax, 4
   cmp bx, ax
   jge .l2

   mov byte [di], 0
   
   leave
   ret
