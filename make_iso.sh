#!/bin/sh

nasm -f bin -o myboot.bin myboot.asm
nasm -f bin -o kernelBase.bin kernelBase.asm

dd status=noxfer conv=notrunc if=myboot.bin of=os2.img
dd status=noxfer conv=notrunc if=kernelBase.bin of=os2.img seek=1 obs=512 ibs=512

#mkisofs -o os1.iso -b os1.img cdiso/

