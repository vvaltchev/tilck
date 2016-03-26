
AS = nasm
CC = gcc
OPT = -O0 -fvisibility=default
INCDIRS = -I./include
CFLAGS =  $(OPT) -std=c99 $(INCDIRS) -m32 -mno-red-zone -ffreestanding -g \
          -nostdinc -fno-builtin  -fno-asynchronous-unwind-tables -fno-zero-initialized-in-bss

DEPDIR := .d
$(shell mkdir -p $(DEPDIR) >/dev/null)

DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td

# used for 'bigobject' compilation
#DEPFLAGS =

BUILD_DIR = ./build

COMPILE.c = $(CC) $(DEPFLAGS) $(CFLAGS) -c
POSTCOMPILE = mv -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d

KERNEL_UNSTRIPPED = $(BUILD_DIR)/uk
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

$(BUILD_DIR)/%.o : %.asm
	nasm -f win32 $(OUTPUT_OPTION) $<

# Build targets for 'bigobject' mode

#$(BUILD_DIR)/bigobject.o: *.c include/*.h
#	cat *.c > bigfile.c
#	$(COMPILE.c) $(OUTPUT_OPTION) bigfile.c
#	rm bigfile.c

#$(KERNEL_TARGET): $(BUILD_DIR)/kernelAsm.o $(BUILD_DIR)/bigobject.o
#	ld -T link.ld -Ttext 0x100000 -s -o $(KERNEL_UNSTRIPPED) $(BUILD_DIR)/kernelAsm.o $(BUILD_DIR)/bigobject.o
#	objcopy -O binary -j .text -j .rdata -j .data $(KERNEL_UNSTRIPPED) $@


build/%.o : %.c
build/%.o : %.c $(DEPDIR)/%.d
	 $(COMPILE.c) $(OUTPUT_OPTION) $<
	 $(POSTCOMPILE)

$(KERNEL_TARGET): $(KERNEL_OBJECTS)
	ld -Ttext 0x100000 -o $(KERNEL_UNSTRIPPED) $(KERNEL_OBJECTS)
	objcopy -O binary -j .text -j .rdata -j .data $(KERNEL_UNSTRIPPED) $@


#objcopy --only-keep-debug $(KERNEL_UNSTRIPPED) $(BUILD_DIR)/kernel.sym

# generate trivial debug info for bochs
#nm $(KERNEL_UNSTRIPPED) | grep " T " | awk '{ print $$1" "$$3 }' > build/kernel.sym
 
$(BOOTLOADER_TARGET): bootloader/boot_stage1.asm bootloader/boot_stage2.asm
	nasm -f bin -o $(BUILD_DIR)/boot_stage1.bin bootloader/boot_stage1.asm
	nasm -f bin -o $(BUILD_DIR)/boot_stage2.bin bootloader/boot_stage2.asm
	cp $(BUILD_DIR)/boot_stage1.bin $(BOOTLOADER_TARGET)
	dd status=noxfer conv=notrunc if=$(BUILD_DIR)/boot_stage2.bin of=$(BOOTLOADER_TARGET) seek=1 obs=512 ibs=512

	
$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

# Comment this for 'bigobject' mode
-include $(patsubst %,$(DEPDIR)/%.d,$(basename $(KERNEL_SOURCES)))