
AS = nasm
CC = gcc
CFLAGS = -m32 -O2 -std=c99 -mno-red-zone -ffreestanding -nostdinc -fno-builtin -fno-asynchronous-unwind-tables

DEPDIR := .d
$(shell mkdir -p $(DEPDIR) >/dev/null)
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td

COMPILE.c = $(CC) $(DEPFLAGS) $(CFLAGS) -c
POSTCOMPILE = mv -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d

KERNEL_TMP_BIN = kernel_binary_tmp
KERNEL_TARGET = kernel32.bin

KERNEL_SOURCES=$(wildcard *.c)
KERNEL_ASM_SOURCES=kernelAsm.asm
KERNEL_OBJECTS=$(KERNEL_ASM_SOURCES:%.asm=%.o) $(KERNEL_SOURCES:%.c=%.o)

BOOTLOADER_SRCS = $(wildcard bootloader/boot_*.asm)
BOOTLOADER_OBJS = $(BOOTLOADER_SRCS:%.asm=%.bin)


all: $(BOOTLOADER_OBJS) $(KERNEL_TARGET)
	dd status=noxfer conv=notrunc if=bootloader/boot_stage1.bin of=os2.img
	dd status=noxfer conv=notrunc if=bootloader/boot_stage2.bin of=os2.img seek=1 obs=512 ibs=512
	dd status=noxfer conv=notrunc if=kernel32.bin of=os2.img seek=9 obs=512 ibs=512

clean:
	rm *.o *.bin bootloader/*.bin

.PHONY: all clean


%.o : %.c
%.o : %.c $(DEPDIR)/%.d
	$(COMPILE.c) $(OUTPUT_OPTION) $<
	$(POSTCOMPILE)

%.o : %.asm
	nasm -f win32 $(OUTPUT_OPTION) $<

boot_%.bin: boot_%.asm
	nasm -f bin $(OUTPUT_OPTION) $<

$(KERNEL_TARGET): $(KERNEL_OBJECTS)
	ld -T link.ld -Ttext 0x100000 -s -o $(KERNEL_TMP_BIN) $(KERNEL_OBJECTS)
	objcopy -O binary -j .text -j .rdata -j .data $(KERNEL_TMP_BIN) $@
	rm $(KERNEL_TMP_BIN)

$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

-include $(patsubst %,$(DEPDIR)/%.d,$(basename $(SRCS)))