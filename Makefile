
AS = nasm
CC = gcc
OPT = -O2
INCDIRS = -I./include
CFLAGS = -m32 $(OPT) -std=c99 $(INCDIRS) -mno-red-zone -ffreestanding \
         -nostdinc -fno-builtin -fno-asynchronous-unwind-tables -fno-zero-initialized-in-bss

DEPDIR := .d
$(shell mkdir -p $(DEPDIR) >/dev/null)
#DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td
DEPFLAGS =

BUILD_DIR = ./build

COMPILE.c = $(CC) $(DEPFLAGS) $(CFLAGS) -c
POSTCOMPILE = mv -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d

KERNEL_TMP_BIN = $(BUILD_DIR)/kernel_binary_tmp
KERNEL_TARGET = $(BUILD_DIR)/kernel32.bin

KERNEL_SOURCES=$(wildcard *.c)
KERNEL_ASM_SOURCES=kernelAsm.asm
KERNEL_OBJECTS=$(KERNEL_ASM_SOURCES:%.asm=build/%.o) $(KERNEL_SOURCES:%.c=build/%.o)

BOOTLOADER_TARGET = $(BUILD_DIR)/bootloader.bin

FINAL_TARGET = os2.img

all: $(BUILD_DIR) $(BOOTLOADER_TARGET) $(KERNEL_TARGET)
	dd status=noxfer conv=notrunc if=$(BOOTLOADER_TARGET) of=$(FINAL_TARGET)
	dd status=noxfer conv=notrunc if=$(KERNEL_TARGET) of=$(FINAL_TARGET) seek=8 obs=512 ibs=512

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
	dd status=noxfer if=/dev/zero of=$(FINAL_TARGET) obs=512 ibs=512 count=2880
	
clean:
	rm -rf $(BUILD_DIR) $(FINAL_TARGET)

# Targets that do not generate files
.PHONY: all clean

#build/%.o : %.c
#build/%.o : %.c $(DEPDIR)/%.d
#	 $(COMPILE.c) $(OUTPUT_OPTION) $<
#	 $(POSTCOMPILE)
	
$(BUILD_DIR)/bigobject.o: *.c include/*.h
	cat *.c > bigfile.c
	$(COMPILE.c) $(OUTPUT_OPTION) bigfile.c
	rm bigfile.c

$(BUILD_DIR)/%.o : %.asm
	nasm -f win32 $(OUTPUT_OPTION) $<


$(BOOTLOADER_TARGET): bootloader/boot_stage1.asm bootloader/boot_stage2.asm
	nasm -f bin -o $(BUILD_DIR)/boot_stage1.bin bootloader/boot_stage1.asm
	nasm -f bin -o $(BUILD_DIR)/boot_stage2.bin bootloader/boot_stage2.asm
	cp $(BUILD_DIR)/boot_stage1.bin $(BOOTLOADER_TARGET)
	dd status=noxfer conv=notrunc if=$(BUILD_DIR)/boot_stage2.bin of=$(BOOTLOADER_TARGET) seek=1 obs=512 ibs=512

$(KERNEL_TARGET): $(BUILD_DIR)/kernelAsm.o $(BUILD_DIR)/bigobject.o
	ld -T link.ld -Ttext 0x100000 -s -o $(KERNEL_TMP_BIN) $(BUILD_DIR)/kernelAsm.o $(BUILD_DIR)/bigobject.o
	objcopy -O binary -j .text -j .rdata -j .data $(KERNEL_TMP_BIN) $@
	#rm $(KERNEL_TMP_BIN)

$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

#-include $(patsubst %,$(DEPDIR)/%.d,$(basename $(KERNEL_SOURCES)))