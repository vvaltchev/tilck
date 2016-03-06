#!/bin/sh

CFLAGS="-O2 -std=c99 -mno-red-zone -ffreestanding -nostdinc -fno-builtin -fno-asynchronous-unwind-tables"

echo "Compiling the bootloader..."
nasm -f bin -o myboot.bin myboot.asm
nasm -f bin -o boot_stage2.bin boot_stage2.asm

echo "Compiling kernel32.c..."
gcc -m32 -c -o kernel32.o $CFLAGS kernel32.c

echo "Compiling kernelAsm.asm"
nasm -f win32 kernelAsm.asm -o kernelAsm.o

echo "Linking the kernel..."
ld -T link.ld -Ttext 0x100000 -s -o kernel_binary_tmp kernelAsm.o kernel32.o 
objcopy -O binary -j .text -j .rdata -j .data kernel_binary_tmp kernel32.bin

echo "Using DD for generating the img file..."
dd status=noxfer conv=notrunc if=myboot.bin of=os2.img
dd status=noxfer conv=notrunc if=boot_stage2.bin of=os2.img seek=1 obs=512 ibs=512
dd status=noxfer conv=notrunc if=kernel32.bin of=os2.img seek=9 obs=512 ibs=512


#objdump -b binary -m i386 -D kernel.bin -M intel
#mkisofs -o os1.iso -b os1.img cdiso/

