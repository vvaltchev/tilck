# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

#
# Project name and base version.
#
# Note: the version here is the one currently *under development*. For released
# versions, checkout GIT tags like "release-0.1.1".
#
project(tilck VERSION 0.1.4 LANGUAGES C CXX ASM)

include(CheckCCompilerFlag)
include(other/cmake/utils.cmake)
include(other/cmake/errors.cmake)

set(CMAKE_VERBOSE_MAKEFILE off)
set(BUILD_SHARED_LIBRARIES off)           # disable shared libs globally
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)    # disable -rdynamic globally
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS)  # disable -rdynamic globally
set(GLOB_CONF_DEP CONFIGURE_DEPENDS)

set(DEFAULT_TARGET_ARCH "i386")

if ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64")

   set(HOST_ARCH "x86_64")
   set(HOST_ARCH_BITS 64)
   set(HOST_ARCH_FAMILY "generic_x86")

elseif ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "amd64")

   set(HOST_ARCH "x86_64")
   set(HOST_ARCH_BITS 64)
   set(HOST_ARCH_FAMILY "generic_x86")

elseif ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "i686")

   set(HOST_ARCH "i386")
   set(HOST_ARCH_BITS 32)
   set(HOST_ARCH_FAMILY "generic_x86")

elseif ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "i386")

   set(HOST_ARCH "i386")
   set(HOST_ARCH_BITS 32)
   set(HOST_ARCH_FAMILY "generic_x86")

elseif("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "aarch64")

   set(HOST_ARCH "aarch64")
   set(HOST_ARCH_BITS 64)
   set(HOST_ARCH_FAMILY "arm")

endif()

if (CMAKE_HOST_SYSTEM_NAME STREQUAL "FreeBSD")
   set(GNU_WRAP_DIR ${CMAKE_SOURCE_DIR}/scripts/gnu-wrap)
   #set(NEW_PATH "${GNU_WRAP_DIR}${CMAKE_ENV_PATH_SEP}${ENV_PATH}")
   #set(ENV{PATH} "${NEW_PATH}")
   #list(PREPEND CMAKE_PROGRAM_PATH "${GNU_WRAP_DIR}")

   #set(ENV_WITH_GNU_WRAP "${CMAKE_COMMAND}" -E env "PATH=${NEW_PATH}")

   ## Apply to everything under this directory (and subdirs unless overridden)
   #set_property(DIRECTORY PROPERTY RULE_LAUNCH_COMPILE "${ENV_WITH_GNU_WRAP}")
   #set_property(DIRECTORY PROPERTY RULE_LAUNCH_LINK    "${ENV_WITH_GNU_WRAP}")

   set(DD ${GNU_WRAP_DIR}/dd)

else()

   set(DD "dd")

endif()

# BEGIN [Environment-controlled "sticky" cache variables]

# Main variables
define_env_cache_str_var(TCROOT_PARENT ${CMAKE_SOURCE_DIR})
define_env_cache_str_var(TCROOT ${TCROOT_PARENT}/toolchain3)
define_env_cache_str_var(ARCH ${DEFAULT_TARGET_ARCH})

define_env_cache_bool_var(TEST_GCOV)
define_env_cache_bool_var(KERNEL_GCOV)

# Board defaults and stick variable
if (${ARCH} STREQUAL "riscv64")
   set(ARCH_DEFAULT_BOARD     "qemu-virt")
else()
   set(ARCH_DEFAULT_BOARD     "")
endif()
define_env_cache_str_var(BOARD "${ARCH_DEFAULT_BOARD}")

# Keep USE_SYSCC as a sticky env variable so that we can fail, if used.
define_env_cache_bool_var(USE_SYSCC)

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


# BEGIN [Setup of toolchain-related variables]

set(BUILD_SCRIPTS       ${CMAKE_SOURCE_DIR}/scripts/build_scripts)
set(BUILD_APPS          ${CMAKE_BINARY_DIR}/scripts/build_apps)
set(BTC_SCRIPT          ${CMAKE_SOURCE_DIR}/scripts/build_toolchain)
set(COVERAGE_HTML_DIR   ${CMAKE_BINARY_DIR}/coverage_html)
set(TCROOT_BA           ${TCROOT}/syscc/host_${HOST_ARCH})
file(RELATIVE_PATH      BTC_SCRIPT_REL ${CMAKE_SOURCE_DIR} ${BTC_SCRIPT})
file(RELATIVE_PATH      TCROOT_REL     ${CMAKE_SOURCE_DIR}     ${TCROOT})

if (NOT EXISTS ${TCROOT})
   message(FATAL_ERROR "Toolchain not found: run ${BTC_SCRIPT_REL} first.")
endif()

# Set the GCC TC config directory
set(GCC_TC_CONF ${CMAKE_SOURCE_DIR}/other/gcc_tc_conf)

# Read the default GCC TC VER from the config file
file(READ ${GCC_TC_CONF}/${ARCH}/default_ver DEFAULT_GCC_TC_VER)

# Define our special "sticky" env. variable GCC_TC_VER
define_env_cache_str_var(GCC_TC_VER "${DEFAULT_GCC_TC_VER}")

#
# GCC_TC_VER check
#

file(READ ${GCC_TC_CONF}/${ARCH}/min_ver MIN_GCC_TC_VER)

if ("${GCC_TC_VER}" VERSION_LESS "${MIN_GCC_TC_VER}")

   set (msg, "")

   string(
      CONCAT msg

      "GCC_TC_VER (${GCC_TC_VER}) < "
      "MIN_GCC_TC_VER (${MIN_GCC_TC_VER}).\n"
      "This probably happened because the minimum GCC version has been bumped "
      "by a source change. It's perfectly normal. Steps to fix:\n"
      "   0. unset \$GCC_TC_VER # if set\n"
      "   1. rm -rf ${CMAKE_BINARY_DIR}\n"
      "   2. ${BTC_SCRIPT_REL} --clean\n"
      "   3. ${BTC_SCRIPT_REL}\n"
      "   4. rebuild"
   )
   message(FATAL_ERROR "${msg}")
   unset(msg)

endif()

# END [Setup of toolchain-related variables]

if (USE_SYSCC)
   message(FATAL_ERROR "USE_SYSCC is not supported anymore")
endif()


#
# Read all the VER_<PACKAGE>=<VALUE> pairs from pkg_versions
#

file(STRINGS ${CMAKE_SOURCE_DIR}/other/pkg_versions PKG_VERSIONS)

foreach(LINE ${PKG_VERSIONS})
   if(NOT "${LINE}" STREQUAL "" AND NOT "${LINE}" MATCHES "^#")

      string(FIND "${LINE}" "=" EQUAL_POS)

      if (EQUAL_POS EQUAL -1)
         message(FATAL_ERROR "Broken key/value syntax in pkg_versions")
      endif()

      string(SUBSTRING "${LINE}" 0 ${EQUAL_POS} KEY)
      math(EXPR VALUE_START_POS "${EQUAL_POS} + 1")
      string(SUBSTRING "${LINE}" ${VALUE_START_POS} -1 VALUE)

      set(${KEY} "${VALUE}")
      message(STATUS "${KEY} = '${VALUE}'")

   endif()
endforeach()

unset(EQUAL_POS)
unset(KEY)
unset(LINE)
unset(VALUE)
unset(VALUE_START_POS)

# Reset the default flags
set(CMAKE_C_FLAGS "")
set(CMAKE_C_FLAGS_INT "")
set(CMAKE_C_FLAGS_DEBUG "")
set(CMAKE_C_FLAGS_MINSIZEREL "")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "")
set(CMAKE_C_FLAGS_RELEASE "")

set(CMAKE_CXX_FLAGS "")
set(CMAKE_CXX_FLAGS_INT "")
set(CMAKE_CXX_FLAGS_DEBUG "")
set(CMAKE_CXX_FLAGS_MINSIZEREL "")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "")
set(CMAKE_CXX_FLAGS_RELEASE "")

# Compile options

# Non-boolean kernel options
set(TIMER_HZ            250 CACHE STRING "System timer HZ")
set(USER_STACK_PAGES     16 CACHE STRING "User apps stack size in pages")
set(TTY_COUNT             2 CACHE STRING "Number of TTYs (default)")
set(MAX_HANDLES          16 CACHE STRING "Max handles/process (keep small)")

set(FBCON_BIGFONT_THR   160 CACHE STRING
    "Max term cols with 8x16 font. After that, a 16x32 font will be used")

# Other non-boolean options

set(FATPART_CLUSTER_SIZE  8 CACHE STRING
    "Cluster size (in sectors) of the FAT ramdisk")

set(TERM_SCROLL_LINES 5 CACHE STRING
    "Number of lines to scroll on Shift+PgUp/PgDown")

set(

   USERAPPS_CFLAGS
   "-ggdb -Os -Wall -Wno-unused-parameter"

   CACHE STRING "Build flags for C/C++ user programs"
)

# Kernel options (enabled by default)

set(KRN_TRACK_NESTED_INTERR ON CACHE BOOL
    "Track the nested interrupts")

set(PANIC_SHOW_STACKTRACE ON CACHE BOOL
    "Show the stacktrace in case of a kernel panic")

set(DEBUG_CHECKS ON CACHE BOOL
    "Keep checking ASSERTs and other debug checks even in release builds")

set(KERNEL_SELFTESTS ON CACHE BOOL
    "Compile-in self-tests in the kernel")

set(KERNEL_STACK_ISOLATION ON CACHE BOOL
    "Put the kernel stack in hi the vmem in isolated pages")

set(FB_CONSOLE_BANNER ON CACHE BOOL
    "Show a top banner when using fb_console")

set(FB_CONSOLE_CURSOR_BLINK ON CACHE BOOL
    "Support cursor blinking in the fb_console")

if ($ENV{TILCK_NO_LOGO})
   set(KERNEL_SHOW_LOGO OFF CACHE BOOL
      "Show Tilck's logo after boot")
else()
   set(KERNEL_SHOW_LOGO ON CACHE BOOL
      "Show Tilck's logo after boot")
endif()

set(KERNEL_SYMBOLS ON CACHE BOOL
    "Keep symbol tables loaded in the kernel for backtraces and self tests")

if (${ARCH} STREQUAL "riscv64")
   set(BOOTLOADER_U_BOOT ON CACHE BOOL
       "Build the U-BOOT bootloader")
else()
   set(BOOTLOADER_LEGACY ON CACHE BOOL
       "Build the legacy bootloader")

   set(BOOTLOADER_EFI ON CACHE BOOL
       "Build the EFI bootloader")
endif()

set(SERIAL_CON_IN_VIDEO_MODE ON CACHE BOOL
    "Always open a console on /dev/ttyS0 even in video mode (w/o -sercon)")

set(KRN_PRINTK_ON_CURR_TTY ON CACHE BOOL
    "Make printk() always flush on the current TTY")

set(TYPICAL_DEVEL_USERAPPS ON CACHE BOOL
    "Include in Tilck's ramdisk some apps for testing the kernel")

set(BOOT_INTERACTIVE ON CACHE BOOL
    "Have user-interactive commands in the bootloaders")

set(KERNEL_64BIT_OFFT ON CACHE BOOL
    "Make offt be 64-bit even on 32-bit systems")

set(KRN_CLOCK_DRIFT_COMP ON CACHE BOOL
    "Compensate periodically for the clock drift in the system time")

set(KRN32_LIN_VADDR ON CACHE BOOL
    "Place the 32-bit kernel in the default linear mapping")

set(USERAPPS_busybox ON CACHE BOOL "Include BUSYBOX (recommended)")

set(TRACE_PRINTK_ENABLED_ON_BOOT ON CACHE BOOL
    "Make trace_printk() to be always enabled since boot time")

# Kernel options (disabled by default)

set(KRN_PAGE_FAULT_PRINTK OFF CACHE BOOL
    "Use printk() to display info when a process is killed due to page fault")

set(KRN_NO_SYS_WARN OFF CACHE BOOL
    "Show a warning when a not-implemented syscall is called")

set(KERNEL_UBSAN OFF CACHE BOOL "Turn on the UBSAN for the kernel")

set(KERNEL_BIG_IO_BUF OFF CACHE BOOL "Use a much-bigger buffer for I/O")

set(TERM_BIG_SCROLL_BUF OFF CACHE BOOL
    "Use a 4x bigger scrollback buffer for the terminal")

set(KERNEL_SYSCC OFF CACHE BOOL
    "Use system's compiler for the kernel instead of toolchain's one")

set(KERNEL_FORCE_TC_ISYSTEM OFF CACHE BOOL "\
DANGEROUS. Do NOT use for other than static analysis builds. \
BRUTALLY forces the build system to use tc's sysroot \
for system headers even when a system compiler is used.\
")

set(FORK_NO_COW OFF CACHE BOOL
    "Make fork() to perform a full-copy instead of using copy-on-write")

set(MMAP_NO_COW OFF CACHE BOOL
    "Make mmap() to allocate real memory instead mapping the zero-page + COW")

set(PANIC_SHOW_REGS OFF CACHE BOOL
    "Show the content of the main registers in case of kernel panic")

set(KMALLOC_HEAVY_STATS OFF CACHE BOOL
    "Count the number of allocations for each distinct size")

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

set(PS2_DO_SELFTEST OFF CACHE BOOL
    "Do PS/2 controller selftests in init_kb()")

set(PS2_VERBOSE_DEBUG_LOG OFF CACHE BOOL
    "Enable developer-only verbose logging")

set(FB_CONSOLE_USE_ALT_FONTS OFF CACHE BOOL
    "Use the fonts in other/alt_fonts instead of the default ones")

set(KRN_RESCHED_ENABLE_PREEMPT OFF CACHE BOOL
    "Check for need_resched and yield in enable_preemption()")

set(KRN_MINIMAL_TIME_SLICE OFF CACHE BOOL
    "Make the time slice to be 1 tick in order to trigger more race conditions")

set(TINY_KERNEL OFF CACHE BOOL "\
Advanced option, use carefully. Forces the Tilck kernel \
to be as small as possible. Incompatibile with many modules \
like console, fb, tracing and several kernel options like \
KERNEL_SELFTESTS")

set(INIT_REPORT_PROC_EXIT OFF CACHE BOOL
    "Make init to report on TTY when children (often acquired) exit")

set(PREFERRED_GFX_MODE_W "800" CACHE STRING
    "Preferred video mode (width)")

set(PREFERRED_GFX_MODE_H "600" CACHE STRING
    "Preferred video mode (height)")

set(KMALLOC_FIRST_HEAP_SIZE_KB "auto" CACHE STRING
    "Size in KB of kmalloc's first heap. Must be multiple of 64.")

set(PCI_VENDORS_LIST OFF CACHE BOOL
    "Compile-in the list of all known PCI vendors")

set(EFI_BOOTLOADER_DEBUG OFF CACHE BOOL
    "Enable an early DEBUG dialog (see docs/debugging.md)")

set(FB_CONSOLE_FAILSAFE_OPT OFF CACHE BOOL
    "Optimize fb_console's failsafe mode for older machines")

# KERNEL_SERCON is different than SERIAL_CON_IN_VIDEO_MODE because it makes
# the serial console and primary and only console. SERIAL_CON_IN_VIDEO_MODE
# simply enables _also_ the serial console even when booted in video mode,
# as a second console.
set(KERNEL_SERCON OFF CACHE BOOL
    "Boot only using the serial console even if we have a console module")

set(KERNEL_SAT OFF CACHE BOOL
    "Enable the sched alive thread on boot (used for CI/CD runs)")

list(
   APPEND no_arch_modules_whitelist

   console
   tracing
   sysfs
   null
)

list(
   APPEND disabled_modules_list

   # No modules in this list, at the moment
)

if (${ARCH} STREQUAL "riscv64")

   list(
      APPEND disabled_modules_list

      # The riscv compiler does not support the acpi module
      acpi
)

endif()

file(GLOB modules ${GLOB_CONF_DEP} "${CMAKE_SOURCE_DIR}/modules/*")

foreach(modpath ${modules})

   get_filename_component(modname ${modpath} NAME_WE)

   if (NOT IS_DIRECTORY ${modpath})
      continue()
   endif()

   list(APPEND modules_list ${modname})
   list(FIND disabled_modules_list ${modname} _index)

   if (${_index} GREATER -1)
      set(MOD_${modname} OFF CACHE BOOL "Compile-in the ${modname} module")
   else()
      set(MOD_${modname} ON CACHE BOOL "Compile-in the ${modname} module")
   endif()

   set(MOD_HEADER_FILE ${CMAKE_SOURCE_DIR}/config/mod_${modname}.h)
   if (NOT EXISTS ${MOD_HEADER_FILE})
      message(FATAL_ERROR "Mod header ${MOD_HEADER_FILE} does not exist")
   endif()
   unset(MOD_HEADER_FILE)

endforeach()

if (TINY_KERNEL)

   if (NOT ${CMAKE_BUILD_TYPE} STREQUAL "MinSizeRel")
      message(FATAL_ERROR "TINY_KERNEL=1 requires CMAKE_BUILD_TYPE=MinSizeRel")
   endif()

   if (KERNEL_GCOV)
      message(FATAL_ERROR "TINY_KERNEL=1 requires KERNEL_GCOV=0")
   endif()

   if (KERNEL_SELFTESTS)
      message(WARNING "TINY_KERNEL=1, expected KERNEL_SELFTESTS=0")
   endif()

   if (KERNEL_SYMBOLS)
      message(WARNING "TINY_KERNEL=1, expected KERNEL_SYMBOLS=0")
   endif()

   if (KERNEL_SHOW_LOGO)
      message(WARNING "TINY_KERNEL=1, expected KERNEL_SHOW_LOGO=0")
   endif()

   if (PCI_VENDORS_LIST)
      message(WARNING "TINY_KERNEL=1, expected PCI_VENDORS_LIST=0")
   endif()

endif()

if (KMALLOC_FIRST_HEAP_SIZE_KB STREQUAL "auto")

   if (TINY_KERNEL)
      set(KMALLOC_FIRST_HEAP_SIZE_KB_VAL 64)
   else()
      set(KMALLOC_FIRST_HEAP_SIZE_KB_VAL 128)
   endif()

else()
   set(KMALLOC_FIRST_HEAP_SIZE_KB_VAL ${KMALLOC_FIRST_HEAP_SIZE_KB})
endif()


# Create a list with all the kernel opts, in order to dump it later
# -------------------------------------------------------------------

list(
   APPEND kernel_opts_list

   # Various options
   ARCH
   BOARD

   # Non-boolean options
   TIMER_HZ
   USER_STACK_PAGES
   FATPART_CLUSTER_SIZE
   PREFERRED_GFX_MODE_W
   PREFERRED_GFX_MODE_H
   KMALLOC_FIRST_HEAP_SIZE_KB
   KMALLOC_FIRST_HEAP_SIZE_KB_VAL

   # Boolean options ENABLED by default
   KRN_TRACK_NESTED_INTERR
   PANIC_SHOW_STACKTRACE
   DEBUG_CHECKS
   KERNEL_SELFTESTS
   KERNEL_STACK_ISOLATION
   FB_CONSOLE_BANNER
   KRN_PRINTK_ON_CURR_TTY
   KERNEL_SHOW_LOGO
   KERNEL_SYMBOLS
   BOOTLOADER_LEGACY
   BOOTLOADER_EFI
   BOOTLOADER_U_BOOT
   BOOT_INTERACTIVE
   KRN_NO_SYS_WARN
   KERNEL_64BIT_OFFT
   KRN_CLOCK_DRIFT_COMP
   KRN32_LIN_VADDR
   USERAPPS_busybox
   TRACE_PRINTK_ENABLED_ON_BOOT

   # Boolean options DISABLED by default
   KERNEL_UBSAN
   KERNEL_BIG_IO_BUF
   KRN_RESCHED_ENABLE_PREEMPT
   TERM_BIG_SCROLL_BUF
   TEST_GCOV
   KERNEL_GCOV
   KERNEL_SYSCC
   KERNEL_FORCE_TC_ISYSTEM
   FORK_NO_COW
   MMAP_NO_COW
   PANIC_SHOW_REGS
   KMALLOC_HEAVY_STATS
   KMALLOC_FREE_MEM_POISONING
   KMALLOC_SUPPORT_DEBUG_LOG
   KMALLOC_SUPPORT_LEAK_DETECTOR
   BOOTLOADER_POISON_MEMORY
   WCONV
   FAT_TEST_DIR
   PS2_DO_SELFTEST
   PS2_VERBOSE_DEBUG_LOG
   FB_CONSOLE_USE_ALT_FONTS
   TINY_KERNEL
   INIT_REPORT_PROC_EXIT
   PCI_VENDORS_LIST
   EFI_BOOTLOADER_DEBUG
   FB_CONSOLE_FAILSAFE_OPT
)

###########################################################


if (${ARCH} STREQUAL "i386")

   set(ARCH_SHORT       "x86")
   set(ARCH_FAMILY      "generic_x86")
   set(ARCH_ELF_NAME    "elf32-i386")
   set(ARCH_LD_OUTPUT   "elf_i386")
   set(ARCH_BFD         "i386")
   set(ARCH_GCC_TC      "i686")
   set(ARCH_GCC_FLAGS   "-march=${ARCH_GCC_TC}")
   set(ARCH_BITS        32)

   # Fundamental kernel MM constants
   set(BASE_VA            0xC0000000)  # Better not touch!
   set(KERNEL_PADDR       0x00100000)  # Better not touch!

   if (KRN32_LIN_VADDR)
      set(LINEAR_MAPPING_MB         896)  # Cannot be > 896 MB because of HiMem
      set(KERNEL_BASE_VA     ${BASE_VA})
      set(KERNEL_VADDR       0xC0100000)  # BASE_VA + KERNEL_PADDR
   else()
      set(LINEAR_MAPPING_MB         880)  # Make room for the kernel and extra
      set(KERNEL_BASE_VA     0xF7000000)  # LINEAR_MAPPING_END
      set(KERNEL_VADDR       0xF7100000)  # LINEAR_MAPPING_END + KERNEL_PADDR
   endif()

elseif (${ARCH} STREQUAL "x86_64")

   set(ARCH_SHORT       "x86_64")
   set(ARCH_FAMILY      "generic_x86")
   set(ARCH_ELF_NAME    "elf64-x86-64")
   set(ARCH_LD_OUTPUT   "elf_x86_64")
   set(ARCH_BFD         "i386:x86-64")
   set(ARCH_GCC_TC      "x86_64")
   set(ARCH_GCC_FLAGS   "")
   set(ARCH_BITS        64)

   # Fundamental kernel MM constants
   set(BASE_VA                0x100000000000)  # +16 TB
   set(KERNEL_PADDR               0x00100000)  # Better not touch (for now)
   set(LINEAR_MAPPING_MB                4096)  # Might be updated.
   set(KERNEL_BASE_VA     0xFFFFFFFF80000000)
   set(KERNEL_VADDR       0xFFFFFFFF80100000)  # -2 GB + 1 MB

elseif (${ARCH} STREQUAL "riscv64")

   set(ARCH_SHORT       "riscv64")
   set(ARCH_FAMILY      "riscv")
   set(ARCH_ELF_NAME    "elf64-littleriscv")
   set(ARCH_LD_OUTPUT   "elf64lriscv")
   set(ARCH_BFD         "riscv:rv64")
   set(ARCH_GCC_TC      "riscv64")
   set(ARCH_GCC_FLAGS   "-march=rv64imafdc -mabi=lp64d -mcmodel=medany")
   set(ARCH_GCC_FLAGS   "${ARCH_GCC_FLAGS} -D__riscv64")
   set(ARCH_BITS        64)

   # Fundamental kernel MM constants
   set(BASE_VA                     0x2000000000)  # +128 GB

   if (KRN32_LIN_VADDR)
      set(LINEAR_MAPPING_MB                 896)  # Cannot be > 896 MB
      set(KERNEL_BASE_VA             ${BASE_VA})
      set(KERNEL_VADDR             0x2000200000)  # BASE_VA + 2MB
   else()
      set(LINEAR_MAPPING_MB                 896)  # todo: Cannot be > 896 MB
      set(KERNEL_BASE_VA     0xFFFFFFFF80000000)  # -2 GB
      set(KERNEL_VADDR       0xFFFFFFFF80200000)  # -2 GB + 2MB
   endif()

else()

   message(FATAL_ERROR "Architecture '${ARCH}' not supported.")

endif()

message(STATUS "TCROOT: ${TCROOT}")
message(STATUS "GCC_TC_VER: ${GCC_TC_VER}")

string(REPLACE                "." "_" GCC_TC_VER_ "${GCC_TC_VER}")
set(TCROOT_ARCH_DIR           ${TCROOT}/${GCC_TC_VER_}/${ARCH})
set(TCROOT_ARCH_DIR_PARENT    ${TCROOT}/${GCC_TC_VER_})

set(BUSYBOX_DIR               ${TCROOT_ARCH_DIR}/busybox/${VER_BUSYBOX})
set(NCURSES_DIR               ${TCROOT_ARCH_DIR}/ncurses)
set(ZLIB_DIR                  ${TCROOT_ARCH_DIR}/zlib/${VER_ZLIB})
set(GNUEFI_DIR                ${TCROOT_ARCH_DIR}/gnuefi/${VER_GNUEFI})
set(LIBFDT_DIR                ${TCROOT_ARCH_DIR}/dtc/${VER_DTC}/libfdt)
set(LCOV_DIR                  ${TCROOT}/noarch/lcov/${VER_LCOV})

set(BUSYBOX_CONFIG            ${BUSYBOX_DIR}/.config)
set(GCC_NAME_PFX              gcc_${GCC_TC_VER_}_${ARCH})

# BEGIN [ARCH-dependent paths]

   # Musl toolchain
   set(GCC_TC_ROOT            ${TCROOT_BA}/${GCC_NAME_PFX}_musl)
   set(GCC_TC_LIBC_DIRNAME    ${ARCH_GCC_TC}-buildroot-linux-musl)
   set(GCC_TC_SYSROOT         ${GCC_TC_ROOT}/${GCC_TC_LIBC_DIRNAME}/sysroot)
   set(GCC_TC_ISYSTEM         ${GCC_TC_SYSROOT}/usr/include)
   set(GCC_TOOLCHAIN          ${GCC_TC_ROOT}/bin)

# END [ARCH-dependent paths]

if (BOARD)
   set(BOARD_BSP        ${CMAKE_SOURCE_DIR}/other/bsp/${ARCH}/${BOARD})
   include(${BOARD_BSP}/board_bsp.cmake)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(msg "SYS C compiler: ${CMAKE_C_COMPILER_ID}")
set(msg "${msg} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "${msg}")

set(msg "SYS CXX compiler: ${CMAKE_CXX_COMPILER_ID}")
set(msg "${msg} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "${msg}")

# Include the main lists of compiler flags used in the project
include(other/cmake/compiler_flags.cmake)

# Include the funcs for building the kernel modules, used both by the kernel
# and the gtests targets.

include(other/cmake/wrapped_syms.cmake)
include(other/cmake/build_modules.cmake)

foreach (opt ${kernel_opts_list})
   message(STATUS "${opt} = ${${opt}}")
endforeach()

message(STATUS "****************** KERNEL MODULES *******************")

foreach (mod ${modules_list})
   message(STATUS "Module ${mod} = ${MOD_${mod}}")
endforeach()

message(STATUS "**************** END KERNEL MODULES *****************")

# Config validation checks
# -----------------------------------

if (NOT ${MOD_console} AND NOT ${MOD_serial})
   message(FATAL_ERROR "Cannot disable both the `console` and `serial` mods")
endif()

foreach (mod ${modules_list})

   set(deps_file "${CMAKE_SOURCE_DIR}/modules/${mod}/module_deps")

   if (NOT ${MOD_${mod}})
      continue() # Module not compiled-in: skip it.
   endif()

   list(APPEND enabled_modules_list ${mod})

   if (NOT EXISTS ${deps_file})
      continue() # No dependencies. Great!
   endif()

   # Read the dependencies in the `deps` list
   file(STRINGS ${deps_file} deps)

   foreach (d ${deps})
      if (NOT ${MOD_${d}})
         message(FATAL_ERROR "The module '${mod}' requires the module '${d}'")
      endif()
   endforeach()

   unset(deps)
   unset(deps_file)

endforeach()

JOIN("${enabled_modules_list}" ${SPACE} ENABLED_MODULES_LIST)
JOIN("${modules_list}" ${SPACE} ALL_MODULES_LIST)
unset(enabled_modules_list)

################################################################################

set(BOOTLOADER_BIN_FILE    ${CMAKE_BINARY_DIR}/boot/legacy/bootloader.bin)

set(EFI_x86_64_FILE        ${CMAKE_BINARY_DIR}/boot/efi/BOOTX64.EFI)
set(EFI_ia32_FILE          ${CMAKE_BINARY_DIR}/boot/efi/BOOTIA32.EFI)
set(KERNEL_UNSTRIPPED      ${CMAKE_BINARY_DIR}/tilck_unstripped)
set(KERNEL_FILE            ${CMAKE_BINARY_DIR}/tilck)
set(IMG_FILE               ${CMAKE_BINARY_DIR}/tilck.img)
set(TILCK_DEVSHELL_PATH    "/initrd/usr/bin/devshell")

if ("${BOOTLOADER_LEGACY}" OR "${BOOTLOADER_EFI}")
   set(KERNEL_BOOTPART_PATH    "tilck")
endif()

if (${BOOTLOADER_U_BOOT})
   set(BOOTCMD_FILE        ${CMAKE_BINARY_DIR}/boot/u_boot/u-boot.cmd)
   set(IMAGE_ITS_FILE      ${CMAKE_BINARY_DIR}/boot/u_boot/fit-image.its)
   set(BOOT_ENVTXT         ${CMAKE_BINARY_DIR}/boot/u_boot/uEnv.txt)
   set(BOOT_SCR            ${CMAKE_BINARY_DIR}/boot.scr)
   set(IMAGE_FIT           ${CMAKE_BINARY_DIR}/image.fit)
   set(U_BOOT_BIN          ${BOARD_BSP_BOOTLOADER})
endif()

################################################################################
# Bootloaders, build-scripts and link-scripts config

set(SECTOR_SIZE              512)  # CHS Logic sector size.  Do *not* touch!
set(CHS_HPC                   16)  # CHS heads per cylinder. Do *not* touch!
set(CHS_SPT                   63)  # CHS sectors per track.  Do *not* touch!
set(BL_ST2_DATA_SEG       0x2000)  # Better not touch!
set(EARLY_BOOT_SZ           4096)  # Better not touch!
set(STAGE3_ENTRY_OFF        4096)  # Better not touch!
set(DISK_UUID         0x4906062b)  # UUID of Tilck's disk image ${IMG_FILE}
set(MAX_BOOTLOADER_BIN_SZ_KB  64)  # Max size of bootloader.bin (keep updated!)
set(IMG_SZ_MB                 36)  # Total size of tilck.img

if (KERNEL_UBSAN)
   set(INITRD_SZ_MB              30)  # Size of the initrd RAMDISK in MB
   set(BOOTPART_SZ_SEC        10240)  # Size of the boot partition in sectors
else()
   set(INITRD_SZ_MB              32)  # Size of the initrd RAMDISK in MB
   set(BOOTPART_SZ_SEC         6144)  # Size of the boot partition in sectors
endif()


math(EXPR IMG_SZ_SEC        "${IMG_SZ_MB} * 1024 * 1024 / ${SECTOR_SIZE}")
math(EXPR BOOT_SECTORS      "${MAX_BOOTLOADER_BIN_SZ_KB}*1024/${SECTOR_SIZE}")
math(EXPR BOOTPART_SEC      "${BOOT_SECTORS}")
math(EXPR BOOTPART_LAST_SEC "${BOOTPART_SEC} + ${BOOTPART_SZ_SEC} - 1")
math(EXPR INITRD_SECTOR     "${BOOTPART_LAST_SEC} + 1")
math(EXPR INITRD_SZ_SEC     "${INITRD_SZ_MB} * 1048576 / ${SECTOR_SIZE}")
math(EXPR INITRD_LAST_SEC   "${INITRD_SECTOR} + ${INITRD_SZ_SEC} - 1")

math(EXPR SEC_PER_CYL   "${CHS_HPC} * ${CHS_SPT}")
math(EXPR CHS_CYLS      "(${IMG_SZ_SEC} + ${SEC_PER_CYL} - 1) / ${SEC_PER_CYL}")
unset(SEC_PER_CYL)

# For qemu run scripts
set(QEMU_CHS_GEOM         "cyls=${CHS_CYLS},heads=${CHS_HPC},secs=${CHS_SPT}")
set(QEMU_RAM_OPT          "")  # use qemu's default: 128 MB
set(QEMU_COMMON_OPTS      "-rtc base=localtime")

if (${ARCH} STREQUAL "i386")

   set(QEMU_ARCH_OPTS      "-device ide-hd,drive=img1,${QEMU_CHS_GEOM$}")

elseif (${ARCH} STREQUAL "x86_64")

   set(QEMU_ARCH_OPTS      "-device ide-hd,drive=img1,${QEMU_CHS_GEOM$}")

elseif (${ARCH} STREQUAL "riscv64")

   set(QEMU_ARCH_OPTS      "-device virtio-blk-device,drive=img1")
   set(QEMU_ARCH_OPTS      "${QEMU_ARCH_OPTS} -M virt")
   set(QEMU_ARCH_OPTS      "${QEMU_ARCH_OPTS} -kernel ${U_BOOT_BIN}")
   set(QEMU_ARCH_OPTS      "${QEMU_ARCH_OPTS} -monitor none")

endif()

# Set kernel's stack size in number of pages.
set(KERNEL_STACK_PAGES          4)

set(FATHACK ${BUILD_APPS}/fathack)

if (${ARCH_BITS} EQUAL 32)
   set(ELFHACK ${BUILD_APPS}/elfhack32)
else()
   set(ELFHACK ${BUILD_APPS}/elfhack64)
endif()

if (USERAPPS_busybox)
   set(DEFAULT_SHELL "/bin/ash")
   set(START_SCRIPT "/initrd/etc/start")
else()
   set(DEFAULT_SHELL "/initrd/usr/bin/devshell")
   set(START_SCRIPT "")
endif()

# Options for extra apps
include(other/cmake/extra_apps.cmake)

# Generate config files & build scripts (before including sub-dirs)
include(other/cmake/gen_config_pre.cmake)

# Include kernel's noarch sources
include(kernel/sources.cmake)

################################################################################
# Include sub-directories

if (BOOTLOADER_LEGACY)
   add_subdirectory(boot/legacy)
endif()

if (BOOTLOADER_EFI)
   add_subdirectory(boot/efi)
endif()

add_subdirectory(userapps)
add_subdirectory(kernel)
add_subdirectory(scripts/build_apps)

################################################################################

# Generate config files & build scripts (after including sub-dirs)
include(other/cmake/gen_config_post.cmake)

set(BUILD_SCRIPTS_FILES_LIST "")
file(GLOB files ${GLOB_CONF_DEP} "${BUILD_SCRIPTS}/*")
foreach (f ${files})
   list(APPEND BUILD_SCRIPTS_FILES_LIST "${file}")
endforeach()
unset(files)

set(dd_opts "status=none" "conv=notrunc")

set(
   mbr_img_deps

   # We MUST depend on BOTH each wrapper-target AND its OUTPUT in order to
   # our image file to be re-built when the *.bin files are re-built.

   kernel
   ${KERNEL_FILE}

   userapps
   ${USERAPPS_FILES_LIST}
   ${EXTRA_APPS_LIST}

   fathack
   mbrhack
   ${CMAKE_BINARY_DIR}/scripts/build_apps/fathack
   ${CMAKE_BINARY_DIR}/scripts/build_apps/mbrhack
   ${CMAKE_SOURCE_DIR}/sysroot/etc/start
   ${CMAKE_BINARY_DIR}/config_fatpart
   ${BUILD_SCRIPTS_FILES_LIST}
)

if (BOOTLOADER_LEGACY)
   list(
      APPEND mbr_img_deps

      bootloader
      ${BOOTLOADER_BIN_FILE}
   )
endif()

if (BOOTLOADER_EFI)

   if (${HOST_ARCH} STREQUAL "i386")

      if (NOT ${ARCH} STREQUAL "i386")
         message(FATAL_ERROR "On i386, we can only build i386 Tilck")
      endif()

      list(APPEND mbr_img_deps efi_bootloader ${EFI_ia32_FILE})

   else()

      list(APPEND mbr_img_deps efi_bootloader ${EFI_x86_64_FILE})

      if (${ARCH} STREQUAL "i386")
         list(APPEND mbr_img_deps ${EFI_ia32_FILE})
      endif()

   endif()
endif()

if (BOOTLOADER_U_BOOT)
   list(
      APPEND mbr_img_deps


      ${BOOT_ENVTXT}
      ${BOOTCMD_FILE}
      ${IMAGE_ITS_FILE}
      ${U_BOOT_BIN}
   )

   set(MKIMAGE ${BOARD_BSP_MKIMAGE})
endif()

# [begin] Set some convenience variables
   set(MBRHACK ${BUILD_APPS}/mbrhack -q ${IMG_FILE})
   set(PARTED parted ${IMG_FILE} -s -a minimal)
   set(CREATE_EMPTY_IMG ${BUILD_SCRIPTS}/create_empty_img_if_necessary)

   # [begin] Setting one long variable, MBRHACK_BPB
      set(MBRHACK_BPB ${SECTOR_SIZE} ${CHS_HPC})
      set(MBRHACK_BPB ${MBRHACK_BPB} ${CHS_SPT} ${IMG_SZ_SEC} ${BOOT_SECTORS})
      set(MBRHACK_BPB ${MBRHACK_BPB} ${BOOT_SECTORS} ${DISK_UUID})
   # [end]
# [end]

message(
   STATUS
   "BOOTPART: [${BOOTPART_SEC}, ${BOOTPART_LAST_SEC}]: +${BOOTPART_SZ_SEC} sec"
)

message(
   STATUS
   "INITRD:   [${INITRD_SECTOR}, ${INITRD_LAST_SEC}]: +${INITRD_SZ_MB} MB"
)

if (BOOTLOADER_LEGACY)
   add_custom_command(
      OUTPUT
         ${IMG_FILE}
      COMMAND
         ${CREATE_EMPTY_IMG} ${IMG_FILE} ${IMG_SZ_MB}
      COMMAND
         ${DD} ${dd_opts} if=${BOOTLOADER_BIN_FILE} of=${IMG_FILE}
      COMMAND
         ${MBRHACK} bpb ${MBRHACK_BPB}
      COMMAND
         ${MBRHACK} add 0x0e ${BOOTPART_SEC} +${BOOTPART_SZ_SEC}
      COMMAND
         ${MBRHACK} add 0x0e ${INITRD_SECTOR} +${INITRD_SZ_MB}M
      COMMAND
         ${BUILD_BOOTPART}
      COMMAND
         ${BUILD_FATPART}
      COMMAND
         ${FATHACK} --truncate fatpart
      COMMAND
         ${FATHACK} --align_first_data_sector fatpart
      COMMAND
         ${DD} ${dd_opts} if=bootpart of=${IMG_FILE} seek=${BOOTPART_SEC}
      COMMAND
         ${DD} ${dd_opts} if=fatpart of=${IMG_FILE} seek=${INITRD_SECTOR}
      DEPENDS
         ${mbr_img_deps}
      COMMENT
         "Creating a dual-bootable (legacy+efi) image file (MBR)"
      VERBATIM
   )
elseif (BOOTLOADER_EFI)
   add_custom_command(
      OUTPUT
         ${IMG_FILE}
      COMMAND
         ${CREATE_EMPTY_IMG} ${IMG_FILE} ${IMG_SZ_MB}
      COMMAND
         ${PARTED} mklabel msdos
      COMMAND
         ${PARTED} mkpart primary FAT16 ${BOOTPART_SEC}s ${BOOTPART_LAST_SEC}s
      COMMAND
         ${PARTED} mkpart primary FAT16 ${INITRD_SECTOR}s ${INITRD_LAST_SEC}s
      COMMAND
         ${BUILD_BOOTPART}
      COMMAND
         ${BUILD_FATPART}
      COMMAND
         ${FATHACK} --truncate fatpart
      COMMAND
         ${FATHACK} --align_first_data_sector fatpart
      COMMAND
         ${DD} ${dd_opts} if=bootpart of=${IMG_FILE} seek=${BOOTPART_SEC}
      COMMAND
         ${DD} ${dd_opts} if=fatpart of=${IMG_FILE} seek=${INITRD_SECTOR}
      DEPENDS
         ${mbr_img_deps}
      COMMENT
         "Creating a EFI-only bootable image file (MBR)"
      VERBATIM
   )
elseif (BOOTLOADER_U_BOOT)
   add_custom_command(
      OUTPUT
         ${IMG_FILE}
      COMMAND
         ${CREATE_EMPTY_IMG} ${IMG_FILE} ${IMG_SZ_MB}
      COMMAND
         ${PARTED} mklabel msdos
      COMMAND
         ${PARTED} mkpart primary FAT16 ${BOOTPART_SEC}s ${BOOTPART_LAST_SEC}s
      COMMAND
         ${PARTED} mkpart primary FAT16 ${INITRD_SECTOR}s ${INITRD_LAST_SEC}s
      COMMAND
         ${BUILD_FATPART}
      COMMAND
         ${FATHACK} --truncate fatpart
      COMMAND
         ${FATHACK} --align_first_data_sector fatpart
      COMMAND
         ${MKIMAGE} -f ${IMAGE_ITS_FILE} ${IMAGE_FIT}
      COMMAND
         ${MKIMAGE} -C none -T script -d ${BOOTCMD_FILE} ${BOOT_SCR}
      COMMAND
         ${BUILD_BOOTPART}
      COMMAND
         ${DD} ${dd_opts} if=bootpart of=${IMG_FILE} seek=${BOOTPART_SEC}
      COMMAND
         ${DD} ${dd_opts} if=fatpart of=${IMG_FILE} seek=${INITRD_SECTOR}
      DEPENDS
         ${mbr_img_deps}
      COMMENT
         "Creating a U-boot fit image"
      VERBATIM
   )
   unset(MKIMAGE)
endif()

# [begin] Unset the convenience variables
   unset(MBRHACK_BPB)
   unset(CREATE_EMPTY_IMG)
   unset(PARTED)
   unset(MBRHACK)
# [end]

add_custom_target(

    tilck_image ALL

    DEPENDS
        ${IMG_FILE}
)

set(GTEST_SOURCE_DIR ${TCROOT}/noarch/gtest/${VER_GTEST}/googletest)
set(GMOCK_SOURCE_DIR ${TCROOT}/noarch/gtest/${VER_GTEST}/googlemock)
set(GTEST_TC_BUILD_DIR ${TCROOT_BA}/gtest/${VER_GTEST})
set(GMOCK_TC_BUILD_DIR ${TCROOT_BA}/gmock/${VER_GTEST})
set(GTEST_BTC_COMMAND "build_gtest build_gmock")

add_custom_command(

   OUTPUT
      test_fatpart

   COMMAND
      ${BUILD_TEST_FATPART}

   COMMAND
      ${BUILD_APPS}/fathack --truncate test_fatpart

   DEPENDS
      fathack
      ${CMAKE_BINARY_DIR}/scripts/build_apps/fathack
)

add_custom_target(

   test_fatpart_target

   DEPENDS
      test_fatpart
)

if (EXISTS ${GTEST_TC_BUILD_DIR} AND EXISTS ${GMOCK_TC_BUILD_DIR})
   add_subdirectory(tests/unit)
   add_dependencies(gtests test_fatpart_target)
else()
   no_googletest_lib_fake_error_target()
endif()

