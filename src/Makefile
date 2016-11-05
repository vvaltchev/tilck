
# Makefile for the kernel

DEPDIR = .d
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td
COMPILE.c = $(CC) $(DEPFLAGS) $(CFLAGS) -c
POSTCOMPILE = mv -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d

KERNEL_UNSTRIPPED = $(BUILD_DIR)/uk
KERNEL_SOURCES=$(wildcard *.c)
KERNEL_ASM_SOURCES=$(wildcard *.asm)

KERNEL_OBJECTS=$(KERNEL_ASM_SOURCES:%.asm=$(BUILD_DIR)/%.o) $(KERNEL_SOURCES:%.c=$(BUILD_DIR)/%.o)

ifdef TEST
	CFLAGS = $(OPT) $(WARN) -std=c99 $(INCDIRS) -fvisibility=default -ffreestanding -g -nostdinc $(ARCH_CFLAGS)
endif

$(shell mkdir -p $(DEPDIR) > /dev/null)
$(shell mkdir -p $(BUILD_DIR) > /dev/null)

$(KERNEL_TARGET): $(KERNEL_OBJECTS)
ifndef TEST
	ld -m $(ELF_LD_FORMAT) -Ttext=0xC0100000 -o $(KERNEL_UNSTRIPPED) $(KERNEL_OBJECTS)
	objcopy -O binary -j .text -j .rodata -j .data $(KERNEL_UNSTRIPPED) $@
else
	$(AR) rcs $(KERNEL_STATIC_LIB_TARGET) $(KERNEL_OBJECTS)
endif

clean:
	rm -rf $(DEPDIR)

$(BUILD_DIR)/%.o : %.asm
	nasm -f $(ELF_NASM_FORMAT) $(OUTPUT_OPTION) $<

$(BUILD_DIR)/%.o : %.c
$(BUILD_DIR)/%.o : %.c $(DEPDIR)/%.d
	 $(COMPILE.c) $(OUTPUT_OPTION) $<
	 $(POSTCOMPILE)

.PHONY: clean

$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

-include $(patsubst %,$(DEPDIR)/%.d,$(basename $(KERNEL_SOURCES)))
