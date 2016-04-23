
# AS = nasm
# CC = gcc
# OPT = -O2 -fvisibility=default -Wall -Wextra
# INCDIRS = -I../include
# CFLAGS =  $(OPT) -std=c99 $(INCDIRS) -m32 -mno-red-zone -ffreestanding -g \
          # -nostdinc -fno-builtin  -fno-asynchronous-unwind-tables \
			 # -fno-zero-initialized-in-bss -Wno-unused-function

# DEPDIR := .d
$(shell mkdir -p $(DEPDIR) >/dev/null)

#DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td

#BUILD_DIR = $(shell pwd)/build

#COMPILE.c = $(CC) $(DEPFLAGS) $(CFLAGS) -c
#POSTCOMPILE = mv -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d

KERNEL_UNSTRIPPED = $(BUILD_DIR)/uk
KERNEL_TARGET = $(BUILD_DIR)/kernel32.bin

KERNEL_SOURCES=$(wildcard *.c)
KERNEL_ASM_SOURCES=$(wildcard *.asm)
KERNEL_OBJECTS=$(KERNEL_ASM_SOURCES:%.asm=$(BUILD_DIR)/%.o) $(KERNEL_SOURCES:%.c=$(BUILD_DIR)/%.o)


$(KERNEL_TARGET): $(KERNEL_OBJECTS)
	ld -T link2.ld -o $(KERNEL_UNSTRIPPED) $(KERNEL_OBJECTS)
	objcopy -O binary -j .text -j .rdata -j .data $(KERNEL_UNSTRIPPED) $@


$(BUILD_DIR)/%.o : %.asm
	nasm -f win32 $(OUTPUT_OPTION) $<


$(BUILD_DIR)/%.o : %.c
$(BUILD_DIR)/%.o : %.c $(DEPDIR)/%.d
	 $(COMPILE.c) $(OUTPUT_OPTION) $<
	 $(POSTCOMPILE)


$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

-include $(patsubst %,$(DEPDIR)/%.d,$(basename $(KERNEL_SOURCES)))