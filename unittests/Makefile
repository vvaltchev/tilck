
# Makefile for kernel's unit tests

DEPDIR = .d
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td
TEST_CFLAGS =  $(OPT) $(WARN) -std=c99 $(INCDIRS) -DTEST $(ARCH_CFLAGS)
COMPILE.c = $(CC) $(DEPFLAGS) $(TEST_CFLAGS) -c
POSTCOMPILE = $(MV) -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d

$(shell mkdir -p $(DEPDIR) > /dev/null)
$(shell mkdir -p $(BUILD_DIR) > /dev/null)

#KERNEL_SOURCES=$(wildcard ../src/*.c)
#KERNEL_SOURCES_FILES=$(patsubst ../src/%,%, $(KERNEL_SOURCES))
#KERNEL_OBJECTS=$(patsubst %.c,$(KERNEL_TEST_BUILD_DIR)/%.o, $(KERNEL_SOURCES_FILES))

TEST_SOURCES=$(wildcard *.c)
TEST_OBJECTS=$(TEST_SOURCES:%.c=$(BUILD_DIR)/%.o)

$(UNITTESTS_TARGET): $(TEST_OBJECTS)
	@echo Linking $@ ...
	@$(CC) $(ARCH_CFLAGS) -o $@ $(TEST_OBJECTS) $(KERNEL_STATIC_LIB_TARGET)

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

-include $(patsubst %,$(DEPDIR)/%.d,$(basename $(TEST_SOURCES)))	
