
# Master Makefile of the project

export AS = nasm
export CC = gcc
export OPT = -O2 -fvisibility=default -Wall -Wextra
export INCDIRS = -I$(shell pwd)/include
export CFLAGS =  $(OPT) -std=c99 $(INCDIRS) -Wall -Wextra -m32 -mno-red-zone     \
                 -ffreestanding -g -nostdinc -fno-builtin -fno-asynchronous-unwind-tables \
			        -fno-zero-initialized-in-bss -Wno-unused-function -Wno-unused-parameter

export BUILD_DIR = $(shell pwd)/build
export BOOTLOADER_TARGET = $(BUILD_DIR)/bootloader.bin
export KERNEL_TARGET = $(BUILD_DIR)/kernel32.bin
export INIT_TARGET = $(BUILD_DIR)/init.bin

export FINAL_TARGET = os2.img


$(FINAL_TARGET): $(BUILD_DIR) $(BOOTLOADER_TARGET) $(KERNEL_TARGET) $(INIT_TARGET)
	dd status=noxfer conv=notrunc if=$(BOOTLOADER_TARGET) of=$@
	dd status=noxfer conv=notrunc if=$(KERNEL_TARGET) of=$@ seek=4 obs=1024 ibs=1024
	dd status=noxfer conv=notrunc if=$(INIT_TARGET) of=$@ seek=132 obs=1024 ibs=1024

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
	dd status=noxfer if=/dev/zero of=$(FINAL_TARGET) obs=512 ibs=512 count=2880

clean:
	cd src && $(MAKE) clean
	cd init_src && $(MAKE) clean
	rm -rf $(BUILD_DIR) $(FINAL_TARGET)

# Targets that do not generate files
.PHONY: clean $(KERNEL_TARGET) $(BOOTLOADER_TARGET) $(INIT_TARGET)

$(BOOTLOADER_TARGET):
	cd bootloader && $(MAKE)

$(KERNEL_TARGET):
	cd src && $(MAKE)

$(INIT_TARGET):
	cd init_src && $(MAKE)

