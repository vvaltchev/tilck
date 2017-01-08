
# Makefile for user mode 'init' program

DEPDIR = .d
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td

# Experimental build of the init program with the musl libc
# The build works fine as the ELF loading, but the programs crashes
# since the kernel does not implement the following syscalls:
#
# - 243 set_thread_area()
# - 123 modify_ldt()
# - 258 set_tid_address()
#
# Likely, after writing a pseudo-fake impl. of those syscalls, other will
# cause troubles as well.

#USE_MUSL = 1

ifdef USE_MUSL
CFLAGS := $(OPT) $(WARN) $(DEFS) -std=c11 $(INCDIRS) $(ARCH_CFLAGS) -g

MUSL_GCC := $(MAIN_PROJ_DIR)/build_scripts/musl-gcc
COMPILE.c = $(MUSL_GCC) $(DEPFLAGS) $(CFLAGS) -c

else

# Default way

CFLAGS := $(OPT) $(WARN) $(DEFS) -std=c11 $(INCDIRS) $(ARCH_CFLAGS) \
                 -ffreestanding -g -nostdinc -fno-builtin \
                 -fno-asynchronous-unwind-tables


COMPILE.c = $(CC) $(DEPFLAGS) $(CFLAGS) -c
endif

POSTCOMPILE = $(MV) -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d

INIT_UNSTRIPPED = $(BUILD_DIR)/ui

SOURCES=$(wildcard *.c)

ifdef USE_MUSL
OBJECTS=$(SOURCES:%.c=$(BUILD_DIR)/%.o)
else
# default
ASM_SOURCES=$(wildcard *.asm)
OBJECTS=$(ASM_SOURCES:%.asm=$(BUILD_DIR)/%.o) $(SOURCES:%.c=$(BUILD_DIR)/%.o)
endif

$(shell mkdir -p $(DEPDIR) >/dev/null)
$(shell mkdir -p $(BUILD_DIR) > /dev/null)

ifdef USE_MUSL
$(INIT_TARGET): $(OBJECTS)
	@echo Linking $@ ...
	$(MUSL_GCC) -Wl,-m -Wl,elf_i386 -static -o $@ $(OBJECTS)
else
#default way
$(INIT_TARGET): $(OBJECTS)
	@echo Linking $@ ...
	@ld -m $(ELF_LD_FORMAT) -o $@ $(OBJECTS)
endif

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
