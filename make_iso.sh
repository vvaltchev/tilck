#!/bin/sh

nasm -f bin -o myboot.bin myboot.asm
nasm -f bin -o boot_stage2.bin boot_stage2.asm

gcc -m32 -c kernel32.c -o kernel32.o -ffreestanding -nostdinc -fno-builtin
ld -r -Ttext 0x0 -s -o kerneltmp kernel32.o
objcopy -O binary -j .text kerneltmp kernel32.bin


dd status=noxfer conv=notrunc if=myboot.bin of=os2.img
dd status=noxfer conv=notrunc if=boot_stage2.bin of=os2.img seek=1 obs=512 ibs=512
dd status=noxfer conv=notrunc if=kernel32.bin of=os2.img seek=17 obs=512 ibs=512


# objdump -b binary -m i386 -D kernel.bin -M intel
#mkisofs -o os1.iso -b os1.img cdiso/

