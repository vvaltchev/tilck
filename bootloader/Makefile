

$(BOOTLOADER_TARGET): boot_stage1.asm boot_stage2.asm
	nasm -f bin -o $(BUILD_DIR)/boot_stage1.bin boot_stage1.asm
	nasm -f bin -o $(BUILD_DIR)/boot_stage2.bin boot_stage2.asm
	cp $(BUILD_DIR)/boot_stage1.bin $(BOOTLOADER_TARGET)
	dd status=noxfer conv=notrunc if=$(BUILD_DIR)/boot_stage2.bin of=$(BOOTLOADER_TARGET) seek=1 obs=512 ibs=512
