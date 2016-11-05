
# Makefile for kernel's main directory

DEPDIR = .d
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td
COMPILE.c = $(CC) $(DEPFLAGS) $(CFLAGS) -c
POSTCOMPILE = $(MV) -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d

KERNEL_UNSTRIPPED = $(BUILD_DIR)/uk
SOURCES=$(wildcard *.c)
ASM_SOURCES=$(wildcard *.asm)
OBJECTS=$(ASM_SOURCES:%.asm=$(BUILD_DIR)/%.o) $(SOURCES:%.c=$(BUILD_DIR)/%.o)

#KERNEL_SOURCES=$(wildcard ../src/*.c)
#KERNEL_SOURCES_FILES=$(patsubst ../src/%,%, $(KERNEL_SOURCES))
#KERNEL_OBJECTS=$(patsubst %.c,$(KERNEL_TEST_BUILD_DIR)/%.o, $(KERNEL_SOURCES_FILES))

#ARCH_SOURCES = $(wildcard $(ARCH)/*.c)
#ARCH_SOURCE_FILES = $(patsubst $(ARCH)/%,%, $(ARCH_SOURCES))
#ARCH_OBJECTS = $(patsubst %.c,$(BUILD_DIR)/$(ARCH)/%.o, $(ARCH_SOURCE_FILES))

ifdef TEST
	export CFLAGS = $(OPT) $(WARN) -std=c99 $(INCDIRS) -fvisibility=default -ffreestanding -g -nostdinc $(ARCH_CFLAGS)
endif

$(shell mkdir -p $(DEPDIR) > /dev/null)
$(shell mkdir -p $(BUILD_DIR) > /dev/null)

$(KERNEL_TARGET): $(KERNEL_ARCH_CODE_STATIC_LIB_TARGET) $(OBJECTS)
	$(AR) rcs $(KERNEL_STATIC_LIB_TARGET) $(OBJECTS)
	
ifndef TEST
	ld -m $(ELF_LD_FORMAT) -Ttext=0xC0100000 -o $(KERNEL_UNSTRIPPED) \
	--start-group $(KERNEL_ARCH_CODE_STATIC_LIB_TARGET) $(KERNEL_STATIC_LIB_TARGET) --end-group
	objcopy -O binary -j .text -j .rodata -j .data $(KERNEL_UNSTRIPPED) $@
else
	
endif


$(KERNEL_ARCH_CODE_STATIC_LIB_TARGET):
	cd $(ARCH) && $(MAKE) BUILD_DIR=$(BUILD_DIR)/$(ARCH)	

clean:
	$(RM) -rf $(DEPDIR)

$(BUILD_DIR)/%.o : %.asm
	$(NASM) -f $(ELF_NASM_FORMAT) $(OUTPUT_OPTION) $<

$(BUILD_DIR)/%.o : %.c
$(BUILD_DIR)/%.o : %.c $(DEPDIR)/%.d
	 $(COMPILE.c) $(OUTPUT_OPTION) $<
	 $(POSTCOMPILE)

.PHONY: clean $(KERNEL_ARCH_CODE_STATIC_LIB_TARGET)

$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

-include $(patsubst %,$(DEPDIR)/%.d,$(basename $(SOURCES)))
