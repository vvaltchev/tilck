cmake_minimum_required(VERSION 3.2)

#########################################################################
# Defines (for our only make build) for working with MUSL_GCC ...
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

file(GLOB SOURCES "*.c")

set(DIET_LIBC_PATH "${CMAKE_SOURCE_DIR}/toolchain/dietlibc-0.33")

include_directories(SYSTEM "${DIET_LIBC_PATH}/include")

add_executable(init "${SOURCES}")

set(EXECUTABLE_OUTPUT_PATH "${CMAKE_BINARY_DIR}")

set_target_properties(

   init

   PROPERTIES
      COMPILE_FLAGS "-D__dietlibc__ ${ARCH_FLAGS} ${COMMON_FLAGS}"
      LINK_FLAGS "${ARCH_FLAGS} -nostdlib ${DIET_LIBC_PATH}/bin-i386/start.o"
)

target_link_libraries(init "${DIET_LIBC_PATH}/bin-i386/dietlibc.a" gcc)
