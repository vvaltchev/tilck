
# Makefile for kernel's main directory

DEPDIR := .d
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td
COMPILE.c = $(CC) $(DEPFLAGS) $(CFLAGS) -c
POSTCOMPILE = $(MV) -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d


KERNEL_UNSTRIPPED := $(BUILD_DIR)/uk
SOURCES:=$(wildcard *.c)
OBJECTS:=$(SOURCES:%.c=$(BUILD_DIR)/%.o)

export KERNEL_ARCH_CODE_STATIC_LIB_TARGET := $(BUILD_DIR)/kernel_arch.a


ifdef TEST
	export CFLAGS = $(OPT) $(WARN) -std=c99 -DKERNEL_TEST $(INCDIRS) -fvisibility=default -ffreestanding -g -nostdinc $(ARCH_CFLAGS)
endif

$(shell mkdir -p $(DEPDIR) > /dev/null)
$(shell mkdir -p $(BUILD_DIR) > /dev/null)

$(KERNEL_TARGET): $(BUILD_DIR)/$(KERNEL_STATIC_LIB_TARGET_NAME) $(OBJECTS)
	@echo Packaging the kernel\'s static library ...
	@$(AR) rcs $(BUILD_DIR)/$(KERNEL_STATIC_LIB_TARGET_NAME) $(OBJECTS)
ifndef TEST
	@echo Linking the unstripped ELF binary of the kernel
	@ld -m $(ELF_LD_FORMAT) -Ttext=0xC0100000 -o $(KERNEL_UNSTRIPPED) \
	--start-group $(KERNEL_ARCH_CODE_STATIC_LIB_TARGET) $(BUILD_DIR)/$(KERNEL_STATIC_LIB_TARGET_NAME) --end-group
	@echo Stripping the kernel\'s binary ...
	@objcopy -O binary -j .text -j .rodata -j .data $(KERNEL_UNSTRIPPED) $@
else
	
endif


$(BUILD_DIR)/$(KERNEL_STATIC_LIB_TARGET_NAME):
	@cd $(ARCH) && $(MAKE) BUILD_DIR=$(BUILD_DIR)/$(ARCH)	

clean:
	$(RM) -rf $(DEPDIR)

$(BUILD_DIR)/%.o : %.c
$(BUILD_DIR)/%.o : %.c $(DEPDIR)/%.d
	@echo Compiling $< ...
	@$(COMPILE.c) $(OUTPUT_OPTION) $<
	@$(POSTCOMPILE)

.PHONY: clean $(KERNEL_ARCH_CODE_STATIC_LIB_TARGET) $(BUILD_DIR)/$(KERNEL_STATIC_LIB_TARGET_NAME)

$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

-include $(patsubst %,$(DEPDIR)/%.d,$(basename $(SOURCES)))
