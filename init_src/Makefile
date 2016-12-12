
# Makefile for user mode 'init' program

DEPDIR = .d
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td
COMPILE.c = $(CC) $(DEPFLAGS) $(CFLAGS) -c
POSTCOMPILE = $(MV) -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d

INIT_UNSTRIPPED = $(BUILD_DIR)/ui

SOURCES=$(wildcard *.c)
ASM_SOURCES=$(wildcard *.asm)
OBJECTS=$(ASM_SOURCES:%.asm=$(BUILD_DIR)/%.o) $(SOURCES:%.c=$(BUILD_DIR)/%.o)

$(shell mkdir -p $(DEPDIR) >/dev/null)
$(shell mkdir -p $(BUILD_DIR) > /dev/null)

$(INIT_TARGET): $(OBJECTS)
	@echo Linking $@ ...
	@ld -m $(ELF_LD_FORMAT) -Ttext=0x08000000 -o $(INIT_UNSTRIPPED) $(OBJECTS)
	@objcopy -O binary -j .text -j .rodata -j .data $(INIT_UNSTRIPPED) $@

$(BUILD_DIR)/%.o : %.asm
	@echo Compiling $< ...
	@$(NASM) -f $(ELF_NASM_FORMAT) $(OUTPUT_OPTION) $<

$(BUILD_DIR)/%.o : %.c
$(BUILD_DIR)/%.o : %.c $(DEPDIR)/%.d
	@echo Compiling $< ...
	@$(COMPILE.c) $(OUTPUT_OPTION) $<
	@$(POSTCOMPILE)

clean:
	$(RM) -rf $(DEPDIR)

.PHONY: clean

$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

-include $(patsubst %,$(DEPDIR)/%.d,$(basename $(SOURCES)))
