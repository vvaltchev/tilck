
# Master Makefile of the project

export AS = nasm
export CC = gcc
export OPT = -O2
#export OPT = -O0 -fno-inline-functions
export WARN = -Wall -Wextra -Wno-unused-function -Wno-unused-parameter
export INCDIRS = -I$(shell pwd)/include
export CFLAGS =  $(OPT) $(WARN) -std=c99 $(INCDIRS) -m32 -march=i686 \
                 -mno-red-zone -fvisibility=default \
                 -ffreestanding -g -nostdinc -fno-builtin \
                 -fno-asynchronous-unwind-tables \
                 -fno-zero-initialized-in-bss

export BUILD_DIR = $(shell pwd)/build
export BOOTLOADER_TARGET = $(BUILD_DIR)/bootloader.bin
export KERNEL_TARGET = $(BUILD_DIR)/kernel32.bin
export INIT_TARGET = $(BUILD_DIR)/init.bin
export FINAL_TARGET = $(BUILD_DIR)/exos.img
export UNITTESTS_TARGET = $(BUILD_DIR)/unittests

$(shell mkdir -p $(BUILD_DIR) > /dev/null)

all: $(FINAL_TARGET)

$(FINAL_TARGET): $(BUILD_DIR) $(BOOTLOADER_TARGET) $(KERNEL_TARGET) $(INIT_TARGET)
	dd status=noxfer conv=notrunc if=$(BOOTLOADER_TARGET) of=$@
	dd status=noxfer conv=notrunc if=$(KERNEL_TARGET) of=$@ seek=4 obs=1024 ibs=1024
	dd status=noxfer conv=notrunc if=$(INIT_TARGET) of=$@ seek=132 obs=1024 ibs=1024

$(BUILD_DIR):
	dd status=noxfer if=/dev/zero of=$(FINAL_TARGET) obs=512 ibs=512 count=2880

tests: $(KERNEL_TARGET)
	cd unittests && $(MAKE)
	
clean:
	cd src && $(MAKE) clean
	cd init_src && $(MAKE) clean
	rm -rf $(BUILD_DIR)

# Targets that do not generate files
.PHONY: clean tests $(KERNEL_TARGET) $(BOOTLOADER_TARGET) $(INIT_TARGET)

$(BOOTLOADER_TARGET):
	cd bootloader && $(MAKE)

$(KERNEL_TARGET):
	cd src && $(MAKE)

$(INIT_TARGET):
	cd init_src && $(MAKE)

