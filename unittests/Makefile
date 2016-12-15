
# Makefile for kernel's UNIT TESTS

DEPDIR = .d
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td

GTEST_DIR = ../toolchain/googletest/googletest
OTHER_INC_DIRS = -I$(GTEST_DIR)/include
LIBS = $(GTEST_DIR)/libgtest.a $(GTEST_DIR)/libgtest_main.a -lpthread

TEST_CFLAGS =  $(OPT) $(WARN) -g $(INCDIRS) $(OTHER_INC_DIRS) -DTESTING $(ARCH_CFLAGS) -Wno-format
COMPILE.c = $(CC) -std=c99 $(DEPFLAGS) $(TEST_CFLAGS) -c
COMPILE.cpp = $(CXX) -std=c++11 $(DEPFLAGS) $(TEST_CFLAGS) -c
POSTCOMPILE = $(MV) -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d

SYMBOL_WRAPPING  = -Wl,--wrap,panic
SYMBOL_WRAPPING += -Wl,--wrap,assert_failed
SYMBOL_WRAPPING += -Wl,--wrap,printk
SYMBOL_WRAPPING += -Wl,--wrap,kbasic_virtual_alloc
SYMBOL_WRAPPING += -Wl,--wrap,kbasic_virtual_free
SYMBOL_WRAPPING += -Wl,--wrap,is_mapped



$(shell mkdir -p $(DEPDIR) > /dev/null)
$(shell mkdir -p $(BUILD_DIR) > /dev/null)

#KERNEL_SOURCES=$(wildcard ../src/*.c)
#KERNEL_SOURCES_FILES=$(patsubst ../src/%,%, $(KERNEL_SOURCES))
#KERNEL_OBJECTS=$(patsubst %.c,$(KERNEL_TEST_BUILD_DIR)/%.o, $(KERNEL_SOURCES_FILES))

TEST_C_SOURCES = $(wildcard *.c)
TEST_CPP_SOURCES = $(wildcard *.cpp)
TEST_OBJECTS=$(TEST_C_SOURCES:%.c=$(BUILD_DIR)/%.o) $(TEST_CPP_SOURCES:%.cpp=$(BUILD_DIR)/%.o) 

$(UNITTESTS_TARGET): $(TEST_OBJECTS)
	@echo Linking $@ ...
	@$(CXX) $(ARCH_CFLAGS) -o $@ $(TEST_OBJECTS) $(LIBS) $(BUILD_DIR)/kernel/$(KERNEL_STATIC_LIB_TARGET_NAME) $(SYMBOL_WRAPPING)

$(BUILD_DIR)/%.o : %.c
$(BUILD_DIR)/%.o : %.c $(DEPDIR)/%.d
	 @echo Compiling $< ...
	 @$(COMPILE.c) $(OUTPUT_OPTION) $<
	 @$(POSTCOMPILE)	


$(BUILD_DIR)/%.o : %.cpp
$(BUILD_DIR)/%.o : %.cpp $(DEPDIR)/%.d
	 @echo Compiling $< ...
	 @$(COMPILE.cpp) $(OUTPUT_OPTION) $<
	 @$(POSTCOMPILE)	
	 
clean:
	$(RM) -rf $(DEPDIR)

.PHONY: clean $(UNITTESTS_TARGET)

$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

-include $(patsubst %,$(DEPDIR)/%.d,$(basename $(TEST_SOURCES)))	
