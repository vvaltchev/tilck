# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)
project (tilck C CXX ASM)

include(CheckCCompilerFlag)

set(CMAKE_VERBOSE_MAKEFILE off)

# Reset the default flags

set(CMAKE_C_FLAGS_DEBUG "")
set(CMAKE_C_FLAGS_MINSIZEREL "")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "")
set(CMAKE_C_FLAGS_RELEASE "")

###########################################################
# Generic CMake Utilities

set(SPACE " ")

function(JOIN VALUES GLUE OUTPUT)
  string (REPLACE ";" "${GLUE}" _TMP_STR "${VALUES}")
  set (${OUTPUT} "${_TMP_STR}" PARENT_SCOPE)
endfunction()

function(PREPEND var prefix)
   set(listVar "")
   foreach(f ${ARGN})
      list(APPEND listVar "${prefix}${f}")
   endforeach(f)
   set(${var} "${listVar}" PARENT_SCOPE)
endfunction(PREPEND)

###########################################################
# Architecture setting. Only i386 is accepted by now.

set(ARCH "i386")

# Compile options
set(USE_GCOV OFF CACHE BOOL "Compile the tests for measuring coverage")

# Non-boolean kernel options
set(TIMER_HZ 100 CACHE STRING "System timer HZ")
set(USER_STACK_PAGES 16 CACHE STRING "User apps stack size in pages")
set(ARCH_GTESTS OFF CACHE BOOL "Build unit tests for target architecture")

# Kernel options (enabled by default)

set(KERNEL_TRACK_NESTED_INTERRUPTS ON CACHE BOOL
    "Track the nested interrupts")

set(TERM_PERF_METRICS ON CACHE BOOL
    "Compile-in code measuring the performance of the console (scrolling)")

set(PANIC_SHOW_STACKTRACE ON CACHE BOOL
    "Show the stacktrace in case of a kernel panic")

set(DEBUG_CHECKS_IN_RELEASE_BUILD ON CACHE BOOL
    "Keep checking ASSERTs and other debug checks even in release builds")

# Kernel options (disabled by default)

set(FORK_NO_COW OFF CACHE BOOL
    "Make fork() to perform a full-copy instead of using copy-on-write")

set(MMAP_NO_COW OFF CACHE BOOL
    "Make mmap() to allocate real memory instead mapping the zero-page + COW")

set(PANIC_SHOW_REGS OFF CACHE BOOL
    "Show the content of the main registers in case of kernel panic")

set(KMALLOC_FREE_MEM_POISONING OFF CACHE BOOL
    "Make kfree() to poison the memory")

set(KMALLOC_SUPPORT_DEBUG_LOG OFF CACHE BOOL
    "Compile-in kmalloc debug messages")

set(KMALLOC_SUPPORT_LEAK_DETECTOR OFF CACHE BOOL
    "Compile-in kmalloc's leak detector")

set(KMALLOC_HEAPS_CREATION_DEBUG OFF CACHE BOOL
    "Make init_kmalloc() to list the heaps")

set(BOOTLOADER_POISON_MEMORY OFF CACHE BOOL
    "Make the bootloader to poison all the available memory")


list(
   APPEND kernel_opts_list

   ARCH_GTESTS
   TIMER_HZ
   KERNEL_TRACK_NESTED_INTERRUPTS
   TERM_PERF_METRICS
   PANIC_SHOW_STACKTRACE
   DEBUG_CHECKS_IN_RELEASE_BUILD

   FORK_NO_COW
   MMAP_NO_COW
   PANIC_SHOW_REGS
   KMALLOC_FREE_MEM_POISONING
   KMALLOC_SUPPORT_DEBUG_LOG
   KMALLOC_SUPPORT_LEAK_DETECTOR
   KMALLOC_HEAPS_CREATION_DEBUG
   BOOTLOADER_POISON_MEMORY
)

###########################################################

set(BUILD_SCRIPTS ${CMAKE_SOURCE_DIR}/scripts/build_scripts)
set(BUILD_APPS ${CMAKE_BINARY_DIR}/scripts/build_apps)
set(BUILD_SHARED_LIBRARIES off)
set(TCROOT ${CMAKE_SOURCE_DIR}/toolchain)
set(BTC_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/build_toolchain)

if (NOT EXISTS ${TCROOT})
   message(
      FATAL_ERROR "Toolchain not found: run ${BTC_SCRIPT} first.")
endif()

if (${ARCH} STREQUAL "i386")
   set(ARCH_FAMILY "generic_x86")
   set(GCC_TOOLCHAIN ${TCROOT}/x86_gcc_toolchain/bin)
   set(GCC_TOOLCHAIN_GLIBC ${TCROOT}/x86_gcc_toolchain_glibc/bin)
else()
   message(FATAL_ERROR "Architecture '${ARCH}' not supported.")
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "C compiler: ${CMAKE_C_COMPILER_ID}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER_ID}")


CHECK_C_COMPILER_FLAG(-fno-tree-loop-vectorize FLAG_NO_TREE_LOOP_VEC_SUPPORTED)
CHECK_C_COMPILER_FLAG(-fno-tree-vectorize FLAG_NO_TREE_VEC_SUPPORTED)
CHECK_C_COMPILER_FLAG(-fno-tree-slp-vectorize FLAG_NO_TREE_SLP_VEC_SUPPORTED)

#message("CMAKE_ASM-ATT_COMPILE_OBJECT: ${CMAKE_ASM-ATT_COMPILE_OBJECT}")
#message("CMAKE_C_LINK_EXECUTABLE: ${CMAKE_C_LINK_EXECUTABLE}")
#message("CMAKE_C_COMPILE_OBJECT: ${CMAKE_C_COMPILE_OBJECT}")
#message("CMAKE_CXX_COMPILE_OBJECT: ${CMAKE_CXX_COMPILE_OBJECT}")

set(GENERAL_DEFS_LIST "")

if (CMAKE_BUILD_TYPE STREQUAL "Release")

   message(STATUS "Preparing a RELEASE build...")
   LIST(APPEND GENERAL_DEFS_LIST "-DNDEBUG -DRELEASE")
   set(OPT_FLAGS_LIST -O2)

else()

   message(STATUS "Preparing a DEBUG build...")
   LIST(APPEND GENERAL_DEFS_LIST "-DDEBUG")
   set(OPT_FLAGS_LIST -O0 -fno-inline-functions)

endif()

if (${USE_GCOV})

   if (NOT EXISTS ${TCROOT}/lcov)
      message(FATAL_ERROR "USE_GCOV is set but no lcov in toolchain. "
                          "Run ${BTC_SCRIPT} -s build_lcov first.")
   endif()

   message(STATUS "USE_GCOV = ON")
   set(GCOV_COMPILE_FLAGS "-fprofile-arcs -ftest-coverage")
   set(GCOV_LINK_FLAGS "-fprofile-arcs -lgcov")
endif()

foreach (opt ${kernel_opts_list})
   message(STATUS "${opt} = ${${opt}}")
endforeach()

set(
   OTHER_CFLAGS_LIST

   -g
)

set(
   WARN_FLAGS_LIST

   -Wall
   -Wextra
   -Werror
   -Wno-unused-function
   -Wno-unused-parameter
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
   if(CMAKE_C_COMPILER_VERSION VERSION_LESS "5.0.0")
      # See http://gcc.gnu.org/bugzilla/show_bug.cgi?id=53119
      list(APPEND WARN_FLAGS_LIST "-Wno-missing-braces")
      list(APPEND WARN_FLAGS_LIST "-Wno-missing-field-initializers")
   endif()
endif()

JOIN("${GENERAL_DEFS_LIST}" ${SPACE} GENERAL_DEFS)
JOIN("${OTHER_CFLAGS_LIST}" ${SPACE} OTHER_CFLAGS)
JOIN("${OPT_FLAGS_LIST}" ${SPACE} OPT_FLAGS)
JOIN("${WARN_FLAGS_LIST}" ${SPACE} WARN_FLAGS)

set(COMMON_FLAGS "${GENERAL_DEFS} ${OTHER_CFLAGS} ${OPT_FLAGS} ${WARN_FLAGS}")

################################################################################

set(BOOTLOADER_BIN_FILE ${CMAKE_BINARY_DIR}/bootloader/bootloader.bin)
set(EFI_x86_64_FILE ${CMAKE_BINARY_DIR}/bootloader/efi/BOOTX64.EFI)
set(EFI_ia32_FILE ${CMAKE_BINARY_DIR}/bootloader/efi/BOOTIA32.EFI)
set(KERNEL_FILE ${CMAKE_BINARY_DIR}/elf_kernel_stripped)
set(IMG_FILE ${CMAKE_BINARY_DIR}/tilck.img)
set(GPT_IMG_FILE ${CMAKE_BINARY_DIR}/gpt_tilck.img)

set(KERNEL_FATPART_PATH "EFI/BOOT/elf_kernel_stripped")
string(REPLACE "/" "\\\\" KERNEL_FATPART_PATH_EFI "${KERNEL_FATPART_PATH}")

################################################################################
# Bootloaders, build-scripts and link-scripts config

set(BL_ST2_DATA_SEG 0x2000)
set(EARLY_BOOT_SZ 4096)
set(STAGE3_ENTRY_OFF 4096)

set(QEMU_RAM_OPT          "")  # use qemu's default: 128 MB
set(QEMU_COMMON_OPTS      "-rtc base=localtime")

set(KERNEL_BASE_VA     0xC0000000)
set(KERNEL_PADDR       0x00100000)
set(LINEAR_MAPPING_MB         768)

# Generate config files & build scripts
include(${CMAKE_SOURCE_DIR}/other/gen_config.cmake)

################################################################################
# Include sub-directories

add_subdirectory(usermode_apps)
add_subdirectory(bootloader)
add_subdirectory(bootloader/efi)
add_subdirectory(kernel)
add_subdirectory(scripts/build_apps)

################################################################################

set(BUILD_FATPART ${CMAKE_BINARY_DIR}/scripts/build_fatpart)

configure_file(
   ${CMAKE_SOURCE_DIR}/scripts/build_scripts/build_fatpart
   ${BUILD_FATPART}
   @ONLY
)

set(BUILD_SCRIPTS_FILES_LIST "")
file(GLOB files "${BUILD_SCRIPTS}/*")
foreach (f ${files})
   list(APPEND BUILD_SCRIPTS_FILES_LIST "${file}")
endforeach()
unset(files)

add_custom_command(

   OUTPUT
      ${IMG_FILE}
   COMMAND
      ${BUILD_SCRIPTS}/create_empty_img_if_necessary ${IMG_FILE} 4096 #37296 # ~36 MB
   COMMAND
      dd status=none conv=notrunc if=${BOOTLOADER_BIN_FILE} of=${IMG_FILE} bs=1K
   COMMAND
      ${BUILD_FATPART}
   COMMAND
      ${BUILD_APPS}/fat_get_used_bytes --truncate fatpart
   COMMAND
      dd status=none conv=notrunc if=${CMAKE_BINARY_DIR}/fatpart of=${IMG_FILE} seek=1024 bs=1K
   DEPENDS
      # We MUST depend on BOTH each wrapper-target AND its OUTPUT in order to
      # our image file to be re-built when the *.bin files are re-built.
      bootloader
      ${BOOTLOADER_BIN_FILE}

      efi_bootloader
      ${EFI_x86_64_FILE}
      ${EFI_ia32_FILE}

      kernel
      ${KERNEL_FILE}

      usermode_apps
      ${USERMODE_APPS_FILES_LIST}
      ${EXTRA_APPS_LIST}

      fat_get_used_bytes
      ${CMAKE_BINARY_DIR}/scripts/build_apps/fat_get_used_bytes
      ${BUILD_SCRIPTS_FILES_LIST}

   COMMENT
      "Creating the bootable image file (MBR)"
   VERBATIM
)

add_custom_target(

    tilck_image ALL

    DEPENDS
        ${IMG_FILE}
)

add_custom_command(

   OUTPUT
      ${GPT_IMG_FILE}
   COMMAND
      ${BUILD_SCRIPTS}/create_empty_img_if_necessary ${GPT_IMG_FILE} 37296 #36MB
   COMMAND
      parted ${GPT_IMG_FILE} -s -a minimal mklabel gpt
   COMMAND
      parted ${GPT_IMG_FILE} -s -a minimal mkpart EFI FAT32 2048s 73727s
   COMMAND
      parted ${GPT_IMG_FILE} -s -a minimal toggle 1 boot
   COMMAND
      dd status=none conv=notrunc if=${CMAKE_BINARY_DIR}/fatpart of=${GPT_IMG_FILE} seek=1024 bs=1K
   DEPENDS
      tilck_image ${IMG_FILE}
   COMMENT
      "Creating the bootable image file (GPT)"
)

add_custom_target(

   gpt_image

   DEPENDS
      ${GPT_IMG_FILE}
)

if (${ARCH_GTESTS})
   set(GTEST_TC_BUILD_DIR ${TCROOT}/gtest_arch)
   set(GTEST_BTC_COMMAND build_gtest_arch)
else()
   set(GTEST_TC_BUILD_DIR ${TCROOT}/gtest)
   set(GTEST_BTC_COMMAND build_gtest)
endif()

if (EXISTS ${GTEST_TC_BUILD_DIR})
   add_subdirectory(unittests)
   add_dependencies(gtests tilck_image)
else()
   add_custom_target(
      gtests
      COMMAND
         echo
      COMMAND
         echo ==== ERROR: No googletest in toolchain =====
      COMMAND
         echo Instructions:
      COMMAND
         echo  - Run ${BTC_SCRIPT} -s ${GTEST_BTC_COMMAND}
      COMMAND
         echo  - rm -rf ${CMAKE_BINARY_DIR}
      COMMAND
         echo  - Run this command again
      COMMAND
         echo
   )
endif()
