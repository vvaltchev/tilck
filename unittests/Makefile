
# Makefile for kernel's unit tests

DEPDIR = .d
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td
TEST_CFLAGS =  $(OPT) $(WARN) -std=c99 $(INCDIRS) -m32 -march=i686
COMPILE.c = $(CC) $(DEPFLAGS) $(TEST_CFLAGS) -c
POSTCOMPILE = mv -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d

$(shell mkdir -p $(DEPDIR) >/dev/null)

TEST_SOURCES=$(wildcard *.c)
TEST_OBJECTS=$(TEST_SOURCES:%.c=$(BUILD_DIR)/%.o)

$(UNITTESTS_TARGET): $(TEST_OBJECTS)
	$(CC) -m32 -o $@ $(TEST_OBJECTS)

$(BUILD_DIR)/%.o : %.c
$(BUILD_DIR)/%.o : %.c $(DEPDIR)/%.d
	 $(COMPILE.c) $(OUTPUT_OPTION) $<
	 $(POSTCOMPILE)	
	
clean:
	rm -rf $(DEPDIR)

.PHONY: clean

$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

-include $(patsubst %,$(DEPDIR)/%.d,$(basename $(TEST_SOURCES)))	