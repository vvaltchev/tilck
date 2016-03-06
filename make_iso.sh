#!/bin/sh

nasm -f bin -o myboot.bin myboot.asm
nasm -f bin -o boot_stage2.bin boot_stage2.asm

CFLAGS="-O0 -std=c99 -mno-red-zone -ffreestanding -nostdinc -fno-builtin -fno-asynchronous-unwind-tables"

gcc -m32 -c -o kernel32.o $CFLAGS kernel32.c 
ld -T link.ld -Ttext 0x22000 -s -o kerneltmp kernel32.o
objcopy -O binary -j .text -j .rdata -j .data kerneltmp kernel32.bin


dd status=noxfer conv=notrunc if=myboot.bin of=os2.img
dd status=noxfer conv=notrunc if=boot_stage2.bin of=os2.img seek=1 obs=512 ibs=512
dd status=noxfer conv=notrunc if=kernel32.bin of=os2.img seek=17 obs=512 ibs=512


#objdump -b binary -m i386 -D kernel.bin -M intel
#mkisofs -o os1.iso -b os1.img cdiso/

