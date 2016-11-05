
# Makefile for kernel's main directory

DEPDIR := .d
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td
COMPILE.c = $(CC) $(DEPFLAGS) $(CFLAGS) -c
POSTCOMPILE = $(MV) -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d


KERNEL_UNSTRIPPED := $(BUILD_DIR)/uk
SOURCES:=$(wildcard *.c)
OBJECTS:=$(SOURCES:%.c=$(BUILD_DIR)/%.o)

ifdef TEST
	export CFLAGS = $(OPT) $(WARN) -std=c99 $(INCDIRS) -fvisibility=default -ffreestanding -g -nostdinc $(ARCH_CFLAGS)
endif

$(shell mkdir -p $(DEPDIR) > /dev/null)
$(shell mkdir -p $(BUILD_DIR) > /dev/null)

$(KERNEL_TARGET): $(KERNEL_ARCH_CODE_STATIC_LIB_TARGET) $(OBJECTS)
	@echo Packaging the kernel\'s static library ...
	@$(AR) rcs $(KERNEL_STATIC_LIB_TARGET) $(OBJECTS)
ifndef TEST
	@echo Linking the unstripped ELF binary of the kernel
	@ld -m $(ELF_LD_FORMAT) -Ttext=0xC0100000 -o $(KERNEL_UNSTRIPPED) \
	--start-group $(KERNEL_ARCH_CODE_STATIC_LIB_TARGET) $(KERNEL_STATIC_LIB_TARGET) --end-group
	@echo Stripping the kernel\'s binary ...
	@objcopy -O binary -j .text -j .rodata -j .data $(KERNEL_UNSTRIPPED) $@
else
	# Do nothing
endif


$(KERNEL_ARCH_CODE_STATIC_LIB_TARGET):
	@cd $(ARCH) && $(MAKE) BUILD_DIR=$(BUILD_DIR)/$(ARCH)	

clean:
	$(RM) -rf $(DEPDIR)

$(BUILD_DIR)/%.o : %.c
$(BUILD_DIR)/%.o : %.c $(DEPDIR)/%.d
	@echo Compiling $< ...
	@$(COMPILE.c) $(OUTPUT_OPTION) $<
	@$(POSTCOMPILE)

.PHONY: clean $(KERNEL_ARCH_CODE_STATIC_LIB_TARGET)

$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

-include $(patsubst %,$(DEPDIR)/%.d,$(basename $(SOURCES)))
