
# Makefile for the bootloader

ASM_SOURCES=$(wildcard *.asm)
OBJECTS=$(ASM_SOURCES:%.asm=$(BUILD_DIR)/%.bin)

$(BUILD_DIR)/%.bin : %.asm
	$(NASM) -f bin $(OUTPUT_OPTION) $<

$(BOOTLOADER_TARGET): $(OBJECTS)
	cp $(BUILD_DIR)/boot_stage1.bin $@
	dd status=noxfer conv=notrunc if=$(BUILD_DIR)/boot_stage2.bin of=$@ seek=1 obs=512 ibs=512
