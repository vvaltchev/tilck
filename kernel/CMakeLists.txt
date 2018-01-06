cmake_minimum_required(VERSION 3.2)

set(
   GENERAL_KERNEL_FLAGS_LIST

   -mno-red-zone
   -ffreestanding
   -nostdinc
   -fno-builtin
   -fno-omit-frame-pointer
)
JOIN("${GENERAL_KERNEL_FLAGS_LIST}" ${SPACE} GENERAL_KERNEL_FLAGS)

set(KERNEL_FLAGS "${COMMON_FLAGS} ${GENERAL_KERNEL_FLAGS}")

include_directories("include")
file(GLOB SOURCES "*.c" "fs/*.c" "self_tests/*.c")

# Creating a static library target for putting kernel's arch-independent code.

add_library(kernel_noarch_static_for_test STATIC EXCLUDE_FROM_ALL ${SOURCES})

set_target_properties(

   kernel_noarch_static_for_test

   PROPERTIES
      COMPILE_FLAGS "-DKERNEL_TEST ${KERNEL_FLAGS} ${GCOV_COMPILE_FLAGS}"
)

add_subdirectory(arch)
