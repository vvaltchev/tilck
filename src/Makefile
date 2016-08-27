
# Makefile for the kernel

DEPDIR = .d
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td
COMPILE.c = $(CC) $(DEPFLAGS) $(CFLAGS) -c
POSTCOMPILE = mv -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d

KERNEL_UNSTRIPPED = $(BUILD_DIR)/uk

KERNEL_SOURCES=$(wildcard *.c)
KERNEL_ASM_SOURCES=$(wildcard *.asm)
KERNEL_OBJECTS=$(KERNEL_ASM_SOURCES:%.asm=$(BUILD_DIR)/%.o) $(KERNEL_SOURCES:%.c=$(BUILD_DIR)/%.o)

$(shell mkdir -p $(DEPDIR) >/dev/null)

$(KERNEL_TARGET): $(KERNEL_OBJECTS)
	ld -Ttext=0xC0100000 -o $(KERNEL_UNSTRIPPED) $(KERNEL_OBJECTS)
	objcopy -O binary -j .text -j .rodata -j .data $(KERNEL_UNSTRIPPED) $@

clean:
	rm -rf $(DEPDIR)

$(BUILD_DIR)/%.o : %.asm
	nasm -f elf $(OUTPUT_OPTION) $<

$(BUILD_DIR)/%.o : %.c
$(BUILD_DIR)/%.o : %.c $(DEPDIR)/%.d
	 $(COMPILE.c) $(OUTPUT_OPTION) $<
	 $(POSTCOMPILE)

.PHONY: clean

$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

-include $(patsubst %,$(DEPDIR)/%.d,$(basename $(KERNEL_SOURCES)))