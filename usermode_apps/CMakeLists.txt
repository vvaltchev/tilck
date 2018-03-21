cmake_minimum_required(VERSION 3.2)

#########################################################################
# Defines (for our only make build) for working with MUSL_GCC ...
#
# Note: currently programs compiled with musl gcc don't work because musl
# requires set_thread_area (syscall #243) to work.
#

# MUSL_GCC := $(MAIN_PROJ_DIR)/scripts/build_scripts/musl-gcc
# COMPILE.c = $(MUSL_GCC) $(DEPFLAGS) $(CFLAGS) -c

# Linking the init target with MUSL_GCC

# $(INIT_TARGET): $(OBJECTS)
# 	@echo Linking $@ ...
# 	$(MUSL_GCC) -Wl,-m -Wl,elf_i386 -static -o $@ $(OBJECTS)

#########################################################################

if (${ARCH} STREQUAL "i386")

   set(CMAKE_C_COMPILER ${GCC_TOOLCHAIN}/i686-buildroot-linux-gnu-gcc)

endif()

set(DIET_LIBC_PATH ${TCROOT}/dietlibc-0.33)
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS) # Remove -rdynamic
include_directories(SYSTEM ${DIET_LIBC_PATH}/include)

link_libraries("${DIET_LIBC_PATH}/bin-i386/dietlibc.a")
link_libraries(gcc)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D__dietlibc__ ${ARCH_FLAGS} ${COMMON_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${ARCH_FLAGS} -nostdlib ${DIET_LIBC_PATH}/bin-i386/start.o")

file(GLOB INIT_SOURCES "init*.c")
add_executable(init ${INIT_SOURCES})

file(GLOB SHELL_SOURCES "shell*.c")
add_executable(shell ${SHELL_SOURCES})
