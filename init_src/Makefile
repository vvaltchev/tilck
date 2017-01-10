
# Makefile for user mode 'init' program

DEPDIR = .d
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td

#########################################################################
# Code for working with MUSL_GCC ...
#
# Note: currently programs compiled with musl gcc don't work because musl
# requires set_thread_area (syscall #243) to work.
#

# MUSL_GCC := $(MAIN_PROJ_DIR)/build_scripts/musl-gcc
# COMPILE.c = $(MUSL_GCC) $(DEPFLAGS) $(CFLAGS) -c

# Linking the init target with MUSL_GCC

# $(INIT_TARGET): $(OBJECTS)
# 	@echo Linking $@ ...
# 	$(MUSL_GCC) -Wl,-m -Wl,elf_i386 -static -o $@ $(OBJECTS)

#########################################################################


CFLAGS := $(OPT) $(WARN) $(DEFS) -std=c11 $(INCDIRS) $(ARCH_CFLAGS) -g -fno-stack-protector
DIET_WRAPPER := $(MAIN_PROJ_DIR)/toolchain/dietlibc-0.33/bin-i386/diet
COMPILE.c = $(DIET_WRAPPER) $(CC) $(DEPFLAGS) $(CFLAGS) -c

POSTCOMPILE = $(MV) -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d

INIT_UNSTRIPPED = $(BUILD_DIR)/ui

SOURCES=$(wildcard *.c)
OBJECTS=$(SOURCES:%.c=$(BUILD_DIR)/%.o)

$(shell mkdir -p $(DEPDIR) >/dev/null)
$(shell mkdir -p $(BUILD_DIR) > /dev/null)

$(INIT_TARGET): $(OBJECTS)
	@echo Linking $@ ...
	$(DIET_WRAPPER) $(CC) -m32 -static -o $@ $(OBJECTS)

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
