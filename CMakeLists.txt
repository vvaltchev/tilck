# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)
project(tilck C CXX ASM)

include(CheckCCompilerFlag)
include(${CMAKE_SOURCE_DIR}/other/utils.cmake)
include(${CMAKE_SOURCE_DIR}/other/errors.cmake)

set(CMAKE_VERBOSE_MAKEFILE off)

set(BUILD_SHARED_LIBRARIES off)           # disable shared libs globally
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)    # disable -rdynamic globally
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS)  # disable -rdynamic globally

set(DEFAULT_TARGET_ARCH "i386")

if ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64")

   set(HOST_ARCH "x86_64")

elseif ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "i686")

   set(HOST_ARCH "i386")

endif()

# BEGIN [Environment-controlled "sticky" cache variables]

# Main variables
define_env_cache_str_var(ARCH ${DEFAULT_TARGET_ARCH})
define_env_cache_bool_var(TEST_GCOV)
define_env_cache_bool_var(KERNEL_GCOV)

# USE_SYSCC-related variables
define_env_cache_bool_var(USE_SYSCC)
define_env_cache_str_var(OBJCOPY "${CMAKE_OBJCOPY}")
define_env_cache_str_var(STRIP "${CMAKE_STRIP}")
define_env_cache_str_var(GCOV "gcov")

# Debug cache variables

# We don't really need to cache the values of CC/CXX, but is useful to that
# in order to prevent user errors. Since CC/CXX affect the build only the first
# time CMake is run, it makes sense to check for the next runs whether the env
# vars CC/CXX changed (if set) by the user or an incorrect script. In case
# they are, we have to stop the build because their new value will be otherwise
# completely ignored. In order words, fail *early*.

define_env_cache_str_var(CC "<unset>")
define_env_cache_str_var(CXX "<unset>")

# END [Environment-controlled "sticky" cache variables]

if (USE_SYSCC)
   set(SYS_CC "${CMAKE_C_COMPILER}")
   set(SYS_CXX "${CMAKE_CXX_COMPILER}")
endif()


if (NOT "${HOST_ARCH}" STREQUAL "x86_64")
   if (NOT "${HOST_ARCH}" STREQUAL "${ARCH}")
      show_wrong_arch_error()
   else()
      if (USE_SYSCC)
         show_same_arch_build_warning()
      else()
         show_missing_use_syscc_error()
      endif()
   endif()
endif()

# Reset the default flags
set(CMAKE_C_FLAGS_DEBUG "")
set(CMAKE_C_FLAGS_MINSIZEREL "")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "")
set(CMAKE_C_FLAGS_RELEASE "")

# Compile options
set(ARCH_GTESTS         OFF CACHE BOOL   "Build unit tests for the target arch")

# Non-boolean kernel options
set(TIMER_HZ            100 CACHE STRING "System timer HZ")
set(USER_STACK_PAGES     16 CACHE STRING "User apps stack size in pages")
set(TTY_COUNT             2 CACHE STRING "Number of TTYs")
set(USERAPPS_CFLAGS "-ggdb" CACHE STRING "Build flags for C/C++ user programs")

# Kernel options (enabled by default)

set(KRN_TRACK_NESTED_INTERR ON CACHE BOOL
    "Track the nested interrupts")

set(PANIC_SHOW_STACKTRACE ON CACHE BOOL
    "Show the stacktrace in case of a kernel panic")

set(DEBUG_CHECKS_IN_RELEASE ON CACHE BOOL
    "Keep checking ASSERTs and other debug checks even in release builds")

set(KERNEL_SELFTESTS ON CACHE BOOL
    "Compile-in self-tests in the kernel")

set(KERNEL_DEBUG_PANEL ON CACHE BOOL
    "Compile-in the debug panel in the kernel")

set(KERNEL_STACK_ISOLATION ON CACHE BOOL
    "Put the kernel stack in hi the vmem in isolated pages")

# Kernel options (disabled by default)

set(KERNEL_SYSCC OFF CACHE BOOL
    "Use system's compiler for the kernel instead of toolchain's one")

set(KERNEL_FORCE_TC_ISYSTEM OFF CACHE BOOL
    "Use TC's sysroot for system headers even when a system compiler is used")

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

set(BOOTLOADER_POISON_MEMORY OFF CACHE BOOL
    "Make the bootloader to poison all the available memory")

set(WCONV OFF CACHE BOOL
    "Compile with -Wconversion when clang is used")

set(FAT_TEST_DIR OFF CACHE BOOL
    "Create a test dir with many files in the FAT32 init ramdisk")

set(KERNEL_DO_PS2_SELFTEST OFF CACHE BOOL
    "Do PS/2 controller selftests in init_kb()")

list(
   APPEND kernel_opts_list

   # Various options
   ARCH
   USE_SYSCC
   ARCH_GTESTS

   # Non-boolean kernel options
   TIMER_HZ
   USER_STACK_PAGES

   # Boolean options ENABLED by default
   KRN_TRACK_NESTED_INTERR
   PANIC_SHOW_STACKTRACE
   DEBUG_CHECKS_IN_RELEASE
   KERNEL_SELFTESTS
   KERNEL_DEBUG_PANEL
   KERNEL_STACK_ISOLATION

   # Boolean options DISABLED by default
   TEST_GCOV
   KERNEL_GCOV
   KERNEL_SYSCC
   KERNEL_FORCE_TC_ISYSTEM
   FORK_NO_COW
   MMAP_NO_COW
   PANIC_SHOW_REGS
   KMALLOC_FREE_MEM_POISONING
   KMALLOC_SUPPORT_DEBUG_LOG
   KMALLOC_SUPPORT_LEAK_DETECTOR
   BOOTLOADER_POISON_MEMORY
   WCONV
   FAT_TEST_DIR
   KERNEL_DO_PS2_SELFTEST
)

###########################################################

set(BUILD_SCRIPTS       ${CMAKE_SOURCE_DIR}/scripts/build_scripts)
set(BUILD_APPS          ${CMAKE_BINARY_DIR}/scripts/build_apps)
set(TCROOT              ${CMAKE_SOURCE_DIR}/toolchain)
set(BTC_SCRIPT          ${CMAKE_SOURCE_DIR}/scripts/build_toolchain)
set(COVERAGE_HTML_DIR   ${CMAKE_BINARY_DIR}/coverage_html)

file(RELATIVE_PATH      BTC_SCRIPT_REL ${CMAKE_SOURCE_DIR} ${BTC_SCRIPT})
file(RELATIVE_PATH      TCROOT_REL     ${CMAKE_SOURCE_DIR}     ${TCROOT})

if (NOT EXISTS ${TCROOT})
   message(FATAL_ERROR "Toolchain not found: run ${BTC_SCRIPT_REL} first.")
endif()

if (${ARCH} STREQUAL "i386")

   set(ARCH_SHORT       "x86")
   set(ARCH_FAMILY      "generic_x86")
   set(ARCH_ELF_NAME    "elf32-i386")
   set(ARCH_LD_OUTPUT   "elf_i386")
   set(ARCH_GCC_TC      "i686")
   set(ARCH_GCC_FLAGS   "-m32 -march=${ARCH_GCC_TC}")
   set(ARCH_TC_DIR      "x86_gcc_toolchain")

else()
   message(FATAL_ERROR "Architecture '${ARCH}' not supported.")
endif()

set(BUSYBOX_CONFIG            ${TCROOT}/busybox/.config)
set(NCURSES_INST              ${TCROOT}/ncurses/install)


# BEGIN [ARCH-dependent paths]

   # Main toolchain (musl)
   set(GCC_TC_ROOT            ${TCROOT}/${ARCH_TC_DIR})
   set(GCC_TC_LIBC_DIRNAME    ${ARCH_GCC_TC}-buildroot-linux-musl)
   set(GCC_TC_SYSROOT         ${GCC_TC_ROOT}/${GCC_TC_LIBC_DIRNAME}/sysroot)
   set(GCC_TC_ISYSTEM         ${GCC_TC_SYSROOT}/usr/include)
   set(GCC_TOOLCHAIN          ${GCC_TC_ROOT}/bin)

   # Glibc toolchain
   set(GCC_TC_GLIBC_ROOT      ${TCROOT}/${ARCH_TC_DIR}_glibc)
   set(GCC_TOOLCHAIN_GLIBC    ${GCC_TC_GLIBC_ROOT}/bin)

# END [ARCH-dependent paths]

if (USE_SYSCC)

   if (NOT EXISTS ${TCROOT}/musl/install/bin/musl-gcc)
      show_no_musl_syscc_error()
   endif()

endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "SYS CC compiler name: ${CMAKE_C_COMPILER_ID}")
message(STATUS "SYS CXX compiler name: ${CMAKE_CXX_COMPILER_ID}")

set(GENERAL_DEFS_LIST "")

if (CMAKE_BUILD_TYPE STREQUAL "Release")

   message(STATUS "Preparing a RELEASE build...")
   list(APPEND GENERAL_DEFS_LIST "-DNDEBUG -DRELEASE")
   set(OPT_FLAGS_LIST -O3)

else()

   message(STATUS "Preparing a DEBUG build...")
   list(APPEND GENERAL_DEFS_LIST "-DDEBUG")
   set(OPT_FLAGS_LIST -O0 -fno-inline-functions)

endif()

if (TEST_GCOV OR KERNEL_GCOV)
   if (NOT EXISTS ${TCROOT}/lcov)
      show_missing_lcov_error()
   endif()
endif()

if (TEST_GCOV)
   set(GCOV_COMPILE_FLAGS "-fprofile-arcs -ftest-coverage")
   set(GCOV_LINK_FLAGS "-fprofile-arcs -lgcov")
endif()

foreach (opt ${kernel_opts_list})
   message(STATUS "${opt} = ${${opt}}")
endforeach()

set(
   OTHER_CFLAGS_LIST

   -ggdb
   -fno-strict-aliasing
)

set(
   WARN_FLAGS_LIST

   -Wall
   -Wextra
   -Werror
   -Wshadow
   -Wno-unused-function
   -Wno-unused-parameter
)

if (CMAKE_C_COMPILER_ID STREQUAL "GNU")

   if (WCONV)
      show_wconv_warning()
   endif()

   if (CMAKE_C_COMPILER_VERSION VERSION_LESS "5.0.0")
      # See http://gcc.gnu.org/bugzilla/show_bug.cgi?id=53119
      list(APPEND WARN_FLAGS_LIST "-Wno-missing-braces")
      list(APPEND WARN_FLAGS_LIST "-Wno-missing-field-initializers")
   endif()
endif()

if (CMAKE_C_COMPILER_ID STREQUAL "Clang")

   list(APPEND WARN_FLAGS_LIST "-Wno-missing-braces")

   if (USE_SYSCC)
      show_clang_and_syscc_error()
   endif()

   if (NOT KERNEL_SYSCC)
      if (WCONV)
         show_wconv_warning()
      endif()
   endif()

endif()


JOIN("${GENERAL_DEFS_LIST}" ${SPACE} GENERAL_DEFS)
JOIN("${OTHER_CFLAGS_LIST}" ${SPACE} OTHER_CFLAGS)
JOIN("${OPT_FLAGS_LIST}"    ${SPACE} OPT_FLAGS)
JOIN("${WARN_FLAGS_LIST}"   ${SPACE} WARN_FLAGS)

set(COMMON_FLAGS "${GENERAL_DEFS} ${OTHER_CFLAGS} ${OPT_FLAGS} ${WARN_FLAGS}")

################################################################################

set(BOOTLOADER_BIN_FILE    ${CMAKE_BINARY_DIR}/boot/legacy/bootloader.bin)

if ("${HOST_ARCH}" STREQUAL "x86_64")
   set(EFI_x86_64_FILE     ${CMAKE_BINARY_DIR}/boot/efi/BOOTX64.EFI)
endif()

set(EFI_ia32_FILE          ${CMAKE_BINARY_DIR}/boot/efi/BOOTIA32.EFI)
set(KERNEL_UNSTRIPPED      ${CMAKE_BINARY_DIR}/tilck_unstripped)
set(KERNEL_FILE            ${CMAKE_BINARY_DIR}/tilck)
set(IMG_FILE               ${CMAKE_BINARY_DIR}/tilck.img)
set(GPT_IMG_FILE           ${CMAKE_BINARY_DIR}/gpt_tilck.img)

set(KERNEL_FATPART_PATH "EFI/BOOT/tilck")
string(REPLACE "/" "\\\\" KERNEL_FATPART_PATH_EFI "${KERNEL_FATPART_PATH}")

################################################################################
# Bootloaders, build-scripts and link-scripts config

set(BL_ST2_DATA_SEG  0x2000)
set(EARLY_BOOT_SZ      4096)
set(STAGE3_ENTRY_OFF   4096)

set(QEMU_RAM_OPT          "")  # use qemu's default: 128 MB
set(QEMU_COMMON_OPTS      "-rtc base=localtime")

set(KERNEL_BASE_VA     0xC0000000)
set(KERNEL_PADDR       0x00100000)
set(LINEAR_MAPPING_MB         896)

# Generate config files & build scripts
include(${CMAKE_SOURCE_DIR}/other/gen_config.cmake)

################################################################################
# Include sub-directories

add_subdirectory(userapps)
add_subdirectory(boot/legacy)
add_subdirectory(boot/efi)
add_subdirectory(kernel)
add_subdirectory(scripts/build_apps)

################################################################################

set(BUILD_FATPART ${CMAKE_BINARY_DIR}/scripts/build_fatpart)
set(BUILD_TEST_FATPART ${CMAKE_BINARY_DIR}/scripts/build_test_fatpart)

configure_file(
   ${CMAKE_SOURCE_DIR}/scripts/templates/build_fatpart
   ${BUILD_FATPART}
   @ONLY
)

configure_file(
   ${CMAKE_SOURCE_DIR}/scripts/templates/build_test_fatpart
   ${BUILD_TEST_FATPART}
   @ONLY
)

set(BUILD_SCRIPTS_FILES_LIST "")
file(GLOB files "${BUILD_SCRIPTS}/*")
foreach (f ${files})
   list(APPEND BUILD_SCRIPTS_FILES_LIST "${file}")
endforeach()
unset(files)

set(dd_opts "status=none" "conv=notrunc")

add_custom_command(

   OUTPUT
      ${IMG_FILE}
   COMMAND
      ${BUILD_SCRIPTS}/create_empty_img_if_necessary ${IMG_FILE} 37296 # ~36 MB
   COMMAND
      dd ${dd_opts} if=${BOOTLOADER_BIN_FILE} of=${IMG_FILE} bs=1K
   COMMAND
      ${BUILD_FATPART}
   COMMAND
      ${BUILD_APPS}/fat_get_used_bytes --truncate fatpart
   COMMAND
      dd ${dd_opts} if=fatpart of=${IMG_FILE} seek=1024 bs=1K
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

      userapps
      ${USERAPPS_FILES_LIST}
      ${EXTRA_APPS_LIST}

      fat_get_used_bytes
      ${CMAKE_BINARY_DIR}/scripts/build_apps/fat_get_used_bytes
      ${CMAKE_SOURCE_DIR}/sysroot/etc/start
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
      dd ${dd_opts} if=fatpart of=${GPT_IMG_FILE} seek=1024 bs=1K
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

if (ARCH_GTESTS)
   set(GTEST_TC_BUILD_DIR ${TCROOT}/gtest_arch)
   set(GTEST_BTC_COMMAND build_gtest_arch)
else()
   set(GTEST_TC_BUILD_DIR ${TCROOT}/gtest)
   set(GTEST_BTC_COMMAND build_gtest)
endif()


add_custom_command(

   OUTPUT
      test_fatpart

   COMMAND
      ${BUILD_TEST_FATPART}

   COMMAND
      ${BUILD_APPS}/fat_get_used_bytes --truncate test_fatpart

   DEPENDS
      fat_get_used_bytes
      ${CMAKE_BINARY_DIR}/scripts/build_apps/fat_get_used_bytes
)

add_custom_target(

   test_fatpart_target

   DEPENDS
      test_fatpart
)

if (EXISTS ${GTEST_TC_BUILD_DIR})
   add_subdirectory(tests/unit)
   add_dependencies(gtests test_fatpart_target)
else()
   no_googletest_lib_fake_error_target()
endif()
