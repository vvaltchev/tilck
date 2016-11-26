
# Makefile for the bootloader

ASM_SOURCES=$(wildcard *.asm)
OBJECTS=$(ASM_SOURCES:%.asm=$(BUILD_DIR)/%.bin)

$(BUILD_DIR)/%.bin : %.asm
	@echo Compiling $< ...
	@$(NASM) -f bin $(OUTPUT_OPTION) $<

$(BOOTLOADER_TARGET): $(OBJECTS)
	@echo Creating $@ ...
	@cp $(BUILD_DIR)/boot.bin $@

