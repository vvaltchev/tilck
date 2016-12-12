
# Makefile for kernel's i386 directory

DEPDIR := .d
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td
COMPILE.c = $(CC) $(DEPFLAGS) $(CFLAGS) -c
POSTCOMPILE = $(MV) -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d

SOURCES := $(wildcard *.c)
ASM_SOURCES := $(wildcard *.asm)
GAS_SOURCES := $(wildcard *.s)
OBJECTS := $(ASM_SOURCES:%.asm=$(BUILD_DIR)/%.o) $(GAS_SOURCES:%.s=$(BUILD_DIR)/%.o) $(SOURCES:%.c=$(BUILD_DIR)/%.o)

$(shell mkdir -p $(DEPDIR) > /dev/null)
$(shell mkdir -p $(BUILD_DIR) > /dev/null)

all: $(KERNEL_ARCH_CODE_STATIC_LIB_TARGET)
	

$(KERNEL_ARCH_CODE_STATIC_LIB_TARGET): $(OBJECTS)
	@echo Packaging kernel\'s i386 static library ...
	@$(AR) rcs $@ $(OBJECTS)
	
clean:
	$(RM) -rf $(DEPDIR)

$(BUILD_DIR)/%.o : %.asm
	@echo Compiling $< ...
	@$(NASM) -f $(ELF_NASM_FORMAT) $(OUTPUT_OPTION) $<

$(BUILD_DIR)/%.o : %.s
	@echo Compiling $< ...
	@$(GAS) --32 $(OUTPUT_OPTION) $<

	
$(BUILD_DIR)/%.o : %.c
$(BUILD_DIR)/%.o : %.c $(DEPDIR)/%.d
	@echo Compiling $< ...
	@$(COMPILE.c) $(OUTPUT_OPTION) $<
	@$(POSTCOMPILE)

.PHONY: clean

$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

-include $(patsubst %,$(DEPDIR)/%.d,$(basename $(SOURCES)))
