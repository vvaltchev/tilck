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
include(ExternalProject)

set(GLOB_CONF_DEP CONFIGURE_DEPENDS)
include(other/cmake/utils.cmake)
include(other/cmake/errors.cmake)

# Truncate the tilck_option() JSONL sidecar exactly once per configure,
# before any sub-project re-includes kernel_options.cmake (or anything
# else that calls tilck_option()). The macro itself guards against
# duplicate emission when called from multiple sub-projects.
tilck_init_options_sidecar()

# Include kernel_options.cmake at the ROOT level so tilck_option()
# calls run during the top-level configure — the sub-projects (kernel,
# gtests) are ExternalProjects configured at build time, so their own
# include() of the same file runs too late to populate the sidecar
# before the validator runs at the end of this file.  set(... CACHE ...)
# is idempotent, so re-inclusion in sub-projects is a no-op for cache
# vars; tilck_option()'s GLOBAL emission guard prevents duplicate
# JSONL records within any single configure run.
include(other/cmake/kernel_options.cmake)

set(BUILD_SHARED_LIBRARIES off)           # disable shared libs globally
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)    # disable -rdynamic globally
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS)  # disable -rdynamic globally

set(CMAKE_VERBOSE_MAKEFILE off)
set(PROJ_ROOT ${CMAKE_SOURCE_DIR})
set(BUILD_DIR ${CMAKE_BINARY_DIR})

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

elseif("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "arm64")

   set(HOST_ARCH "aarch64")
   set(HOST_ARCH_BITS 64)
   set(HOST_ARCH_FAMILY "arm")

endif()

if (CMAKE_HOST_SYSTEM_NAME STREQUAL "FreeBSD")
   set(GNU_WRAP_DIR ${CMAKE_SOURCE_DIR}/scripts/gnu-wrap)
   set(DD ${GNU_WRAP_DIR}/dd)
elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
   set(GNU_WRAP_DIR ${CMAKE_SOURCE_DIR}/scripts/gnu-wrap)
   set(DD ${GNU_WRAP_DIR}/dd)
else()
   set(DD "dd")
endif()

# Host environment detection.
#
#   HOST_OS      = "linux" | "macos" | "freebsd"
#   HOST_DISTRO  = "ubuntu-22.04" | "macos-14.3" | "freebsd-14.0" | ...
#   HOST_CC      = "gcc-13.3.0" | "clang-14.0.0" | ...
#
# These values are used to construct the toolchain4 host tool paths
# (HOST_DIR_PORTABLE / HOST_DIR below). They MUST stay in sync with the
# Ruby pkgmgr implementation in scripts/pkgmgr/early_logic.rb (InitOnly
# module: get_host_os, get_host_distro, get_host_cc), otherwise CMake
# will look for host tools in a different subtree than where the
# package manager installed them.
if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")

   set(HOST_OS "linux")

   if (NOT EXISTS "/etc/os-release")
      message(FATAL_ERROR "/etc/os-release not found: cannot detect distro")
   endif()

   file(STRINGS "/etc/os-release" _id_line  REGEX "^ID=")
   file(STRINGS "/etc/os-release" _ver_line REGEX "^VERSION_ID=")
   string(REPLACE "ID="         "" _id  "${_id_line}")
   string(REPLACE "VERSION_ID=" "" _ver "${_ver_line}")
   string(REPLACE "\""          "" _id  "${_id}")
   string(REPLACE "\""          "" _ver "${_ver}")

   if ("${_id}" STREQUAL "" OR "${_ver}" STREQUAL "")
      message(FATAL_ERROR "/etc/os-release is missing ID or VERSION_ID")
   endif()

   set(HOST_DISTRO "${_id}-${_ver}")
   unset(_id_line)
   unset(_ver_line)
   unset(_id)
   unset(_ver)

elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")

   set(HOST_OS "macos")
   execute_process(
      COMMAND sw_vers -productVersion
      OUTPUT_VARIABLE _mac_ver
      OUTPUT_STRIP_TRAILING_WHITESPACE
   )
   if ("${_mac_ver}" STREQUAL "")
      message(FATAL_ERROR "sw_vers -productVersion returned nothing")
   endif()
   # Use only major.minor (e.g. "26.4") so a minor-patch bump like
   # 26.4 -> 26.4.1 doesn't orphan the distro subtree.
   string(REGEX REPLACE "^([0-9]+\\.[0-9]+).*" "\\1" _mac_ver "${_mac_ver}")
   set(HOST_DISTRO "macos-${_mac_ver}")
   unset(_mac_ver)

elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "FreeBSD")

   set(HOST_OS "freebsd")
   execute_process(
      COMMAND uname -r
      OUTPUT_VARIABLE _fbsd_ver
      OUTPUT_STRIP_TRAILING_WHITESPACE
   )
   string(REGEX REPLACE "^([^-]+).*$" "\\1" _fbsd_ver "${_fbsd_ver}")
   if ("${_fbsd_ver}" STREQUAL "")
      message(FATAL_ERROR "uname -r returned nothing")
   endif()
   set(HOST_DISTRO "freebsd-${_fbsd_ver}")
   unset(_fbsd_ver)

else()
   message(FATAL_ERROR "Unsupported host OS: ${CMAKE_HOST_SYSTEM_NAME}")
endif()

# HOST_CC: family+version of the host C compiler detected by project().
# Must match the Ruby pkgmgr's get_host_cc, which reads `$CC -v` output.
#
# Apple Clang reports a 4-component version to CMake (e.g. 21.0.0.21000099)
# while `clang -v` says "Apple clang version 21.0.0 ...". The Ruby pkgmgr
# parses the latter, so truncate to major.minor.patch here to stay in sync.
if (CMAKE_C_COMPILER_ID MATCHES "Clang")
   string(REGEX REPLACE
      "^([0-9]+\\.[0-9]+\\.[0-9]+).*" "\\1"
      _clang_ver "${CMAKE_C_COMPILER_VERSION}"
   )
   set(HOST_CC "clang-${_clang_ver}")
elseif (CMAKE_C_COMPILER_ID STREQUAL "GNU")
   set(HOST_CC "gcc-${CMAKE_C_COMPILER_VERSION}")
else()
   message(
      FATAL_ERROR
      "Unsupported host C compiler: ${CMAKE_C_COMPILER_ID}"
   )
endif()

# BEGIN [Environment-controlled "sticky" cache variables]

# Main variables
define_env_cache_str_var(TCROOT_PARENT ${CMAKE_SOURCE_DIR})
define_env_cache_str_var(TCROOT ${TCROOT_PARENT}/toolchain4)
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

# Host-side install roots in the toolchain4 layout.
#
# HOST_DIR_PORTABLE — statically-linked, distro-agnostic host tools (cross
#    compilers). Shared across distros and host compilers; a GCC_TC_VER
#    bump does not invalidate extracted tarballs here.
#
# HOST_DIR         — non-portable host tools (host_mtools, host_gtest) whose
#    binaries or libraries depend on glibc/libstdc++ from a specific distro,
#    built with a specific host compiler. Because C++ has no stable ABI, the
#    host-compiler version is part of the path even for C-only tools that
#    may pull in C++ host libs.
set(HOST_DIR_PORTABLE   ${TCROOT}/host/${HOST_OS}-${HOST_ARCH}/portable)
set(HOST_DIR_DISTRO     ${TCROOT}/host/${HOST_OS}-${HOST_ARCH}/${HOST_DISTRO})
set(HOST_DIR            ${HOST_DIR_DISTRO}/${HOST_CC})
set(CMAKE_DIR           ${CMAKE_SOURCE_DIR}/other/cmake)
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

# Re-run CMake automatically when pkg_versions changes (e.g. version bump)
# so that the upgrade check below fires and VER_* variables are refreshed.
set_property(DIRECTORY APPEND PROPERTY
   CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/other/pkg_versions)

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
      list(APPEND PKG_VERSION_VARS ${KEY})
      message(STATUS "${KEY} = '${VALUE}'")

   endif()
endforeach()

unset(EQUAL_POS)
unset(KEY)
unset(LINE)
unset(VALUE)
unset(VALUE_START_POS)

#
# Lightweight check for package upgrades. Calls the Ruby pkgmgr directly
# (no bash wrapper, no system-package checks) so it adds <100ms to cmake.
# Exit code 0 = up to date, 2 = upgrades needed.
#
# The Ruby binary may live in the toolchain (downloaded/built by the bash
# bootstrap) or be the system Ruby (used when the system version >= 3.2).
# Try the toolchain path first, fall back to system `ruby`.
#
set(PKGMGR_RUBY "")
set(_tc_ruby
   ${TCROOT}/host/${HOST_OS}-${HOST_ARCH}/${HOST_DISTRO}/ruby/${VER_RUBY}/bin/ruby)

if (EXISTS ${_tc_ruby})
   set(PKGMGR_RUBY ${_tc_ruby})
else()
   find_program(PKGMGR_RUBY ruby)
endif()
unset(_tc_ruby)

set(PKGMGR_MAIN ${CMAKE_SOURCE_DIR}/scripts/pkgmgr/main.rb)

if (PKGMGR_RUBY AND EXISTS ${PKGMGR_MAIN})
   execute_process(
      COMMAND ${PKGMGR_RUBY} ${PKGMGR_MAIN} -q --check-for-updates
      RESULT_VARIABLE _upgrade_rc
      OUTPUT_VARIABLE _upgrade_out
      OUTPUT_STRIP_TRAILING_WHITESPACE
   )
   if (_upgrade_rc EQUAL 2)
      message(FATAL_ERROR
         "\nSome installed toolchain packages need upgrading:\n"
         "   ${_upgrade_out}\n\n"
         "Run: ${BTC_SCRIPT_REL} --upgrade\n")
   endif()
   unset(_upgrade_rc)
   unset(_upgrade_out)
endif()

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

# =====================================================================
# Shared options: exposed to the kernel + tests/unit sub-projects
# via the _krn_forward_args forwarding block below (any KRN_* OR any
# cache variable CMake considers a shared option). Migrated to
# tilck_option() so they show up in scripts/run_config's menuconfig.
# =====================================================================

# --- Graphics ---

tilck_option(PREFERRED_GFX_MODE_W
   TYPE     UINT
   CATEGORY "Misc"
   DEFAULT  800
   HELP     "Preferred video mode (width, pixels)"
)

tilck_option(PREFERRED_GFX_MODE_H
   TYPE     UINT
   CATEGORY "Misc"
   DEFAULT  600
   HELP     "Preferred video mode (height, pixels)"
)

# --- Filesystems ---

tilck_option(FATPART_CLUSTER_SIZE
   TYPE     UINT
   CATEGORY "Misc"
   DEFAULT  8
   HELP     "FAT ramdisk cluster size (sectors)"
)

tilck_option(FAT_TEST_DIR
   TYPE     BOOL
   CATEGORY "Misc"
   DEFAULT  OFF
   HELP     "Add a test dir with many files to the FAT32 initrd"
)

# --- Bootloader (arch-dependent) ---

if (${ARCH} STREQUAL "riscv64")

   tilck_option(BOOTLOADER_U_BOOT
      TYPE     BOOL
      CATEGORY "Bootloader"
      DEFAULT  ON
      HELP     "Build the U-Boot bootloader"
   )

else()

   tilck_option(BOOTLOADER_LEGACY
      TYPE     BOOL
      CATEGORY "Bootloader"
      DEFAULT  ON
      HELP     "Build the legacy BIOS bootloader"
   )

   tilck_option(BOOTLOADER_EFI
      TYPE     BOOL
      CATEGORY "Bootloader"
      DEFAULT  ON
      HELP     "Build the UEFI bootloader"
   )

   tilck_option(EFI_BOOTLOADER_DEBUG
      TYPE     BOOL
      CATEGORY "Bootloader"
      DEFAULT  OFF
      DEPENDS  BOOTLOADER_EFI
      HELP     "Early DEBUG dialog in EFI bootloader"
               "See docs/debugging.md. Only meaningful when the EFI"
               "bootloader is being built."
   )

endif()

tilck_option(BOOT_INTERACTIVE
   TYPE     BOOL
   CATEGORY "Bootloader"
   DEFAULT  ON
   HELP     "Enable interactive bootloader commands"
)

tilck_option(BOOTLOADER_POISON_MEMORY
   TYPE     BOOL
   CATEGORY "Bootloader"
   DEFAULT  OFF
   HELP     "Poison all available memory in the bootloader"
)

# --- Userapps ---

tilck_option(USERAPPS_busybox
   TYPE     BOOL
   CATEGORY "Userapps"
   DEFAULT  ON
   HELP     "Include BusyBox in the image (recommended)"
)

tilck_option(TYPICAL_DEVEL_USERAPPS
   TYPE     BOOL
   CATEGORY "Userapps"
   DEFAULT  ON
   HELP     "Include kernel-testing dev apps in the ramdisk"
)

tilck_option(USERAPPS_CFLAGS
   TYPE     STRING
   CATEGORY "Userapps"
   DEFAULT  "-ggdb -Os -Wall -Wno-unused-parameter"
   HELP     "Build flags for C/C++ user programs"
)

# --- Kernel / Memory ---

tilck_option(KERNEL_64BIT_OFFT
   TYPE     BOOL
   CATEGORY "Kernel Memory"
   DEFAULT  ON
   HELP     "Make off_t 64-bit even on 32-bit targets"
)

tilck_option(FORK_NO_COW
   TYPE     BOOL
   CATEGORY "Kernel Memory"
   DEFAULT  OFF
   HELP     "fork(): full memory copy instead of copy-on-write"
)

tilck_option(MMAP_NO_COW
   TYPE     BOOL
   CATEGORY "Kernel Memory"
   DEFAULT  OFF
   HELP     "mmap(): allocate real memory instead of zero-page + COW"
)

tilck_option(KRN32_LIN_VADDR
   TYPE     BOOL
   CATEGORY "Kernel Memory"
   DEFAULT  ON
   HELP     "Place the 32-bit kernel at the default linear mapping"
)

# --- Kernel / Terminal ---

tilck_option(TTY_COUNT
   TYPE     UINT
   CATEGORY "Kernel Terminal"
   DEFAULT  2
   HELP     "Number of virtual TTYs"
)

tilck_option(TERM_BIG_SCROLL_BUF
   TYPE     BOOL
   CATEGORY "Kernel Terminal"
   DEFAULT  OFF
   HELP     "Use a 4x larger scrollback buffer"
)

# KERNEL_SERCON vs SERIAL_CON_IN_VIDEO_MODE: the first makes the
# serial port the primary and ONLY console; the second enables an
# additional console on /dev/ttyS0 alongside the video console.
tilck_option(SERIAL_CON_IN_VIDEO_MODE
   TYPE     BOOL
   CATEGORY "Kernel Terminal"
   DEFAULT  ON
   HELP     "Also open /dev/ttyS0 console in video mode"
)

tilck_option(KERNEL_SERCON
   TYPE     BOOL
   CATEGORY "Kernel Terminal"
   DEFAULT  OFF
   HELP     "Use serial as the only console"
            "Forces the kernel to boot with the serial console as the"
            "primary (and only) console, even when a video console"
            "module is available."
)

# --- Kernel / Debug ---

tilck_option(DEBUG_CHECKS
   TYPE     BOOL
   CATEGORY "Kernel Debug"
   DEFAULT  ON
   HELP     "Keep ASSERTs + debug checks in release builds"
)

tilck_option(KERNEL_SELFTESTS
   TYPE     BOOL
   CATEGORY "Kernel Debug"
   DEFAULT  ON
   HELP     "Compile-in the kernel's self-tests"
)

tilck_option(PANIC_SHOW_STACKTRACE
   TYPE     BOOL
   CATEGORY "Kernel Debug"
   DEFAULT  ON
   HELP     "Show the stacktrace on kernel panic"
)

tilck_option(PANIC_SHOW_REGS
   TYPE     BOOL
   CATEGORY "Kernel Debug"
   DEFAULT  OFF
   HELP     "Show register contents on kernel panic"
)

tilck_option(KERNEL_UBSAN
   TYPE     BOOL
   CATEGORY "Kernel Debug"
   DEFAULT  OFF
   HELP     "Enable UBSAN instrumentation in the kernel"
)

tilck_option(KERNEL_SAT
   TYPE     BOOL
   CATEGORY "Kernel Debug"
   DEFAULT  OFF
   HELP     "Enable the sched-alive thread at boot (CI runs)"
)

tilck_option(PS2_DO_SELFTEST
   TYPE     BOOL
   CATEGORY "Kernel Debug"
   DEFAULT  OFF
   HELP     "Run PS/2 controller selftests in init_kb()"
)

tilck_option(PS2_VERBOSE_DEBUG_LOG
   TYPE     BOOL
   CATEGORY "Kernel Debug"
   DEFAULT  OFF
   HELP     "Enable verbose PS/2 debug logging (dev-only)"
)

tilck_option(INIT_REPORT_PROC_EXIT
   TYPE     BOOL
   CATEGORY "Kernel Debug"
   DEFAULT  OFF
   HELP     "Report child-process exits on init's TTY"
)

# --- Kernel / Misc ---

tilck_option(KERNEL_SYSCC
   TYPE     BOOL
   CATEGORY "Kernel Misc"
   DEFAULT  OFF
   HELP     "Use system compiler instead of the toolchain's"
)

tilck_option(KERNEL_FORCE_TC_ISYSTEM
   TYPE     BOOL
   CATEGORY "Kernel Misc"
   DEFAULT  OFF
   HELP     "Force toolchain sysroot with a system compiler (DANGEROUS)"
            "Only meant for static-analysis builds. Brutally forces the"
            "build system to use the toolchain sysroot for system"
            "headers even when a system compiler is in use."
)

tilck_option(WCONV
   TYPE     BOOL
   CATEGORY "Kernel Misc"
   DEFAULT  OFF
   HELP     "Compile with -Wconversion (clang only)"
)

list(
   APPEND no_arch_modules_whitelist

   console
   tracing
   sysfs
   null
)

list(
   APPEND disabled_modules_list

   # e1000 driver still in development
   e1000
)

if (${ARCH} STREQUAL "riscv64")

   list(
      APPEND disabled_modules_list

      # The riscv compiler does not support the acpi module
      acpi
)

endif()

file(GLOB modules ${GLOB_CONF_DEP} "${PROJ_ROOT}/modules/*")

foreach(modpath ${modules})

   get_filename_component(modname ${modpath} NAME_WE)

   if (NOT IS_DIRECTORY ${modpath})
      continue()
   endif()

   list(APPEND modules_list ${modname})
   list(FIND disabled_modules_list ${modname} _index)

   if (${_index} GREATER -1)
      set(_mod_default OFF)
   else()
      set(_mod_default ON)
   endif()

   # Read modules/<mod>/module_deps (one dep per line, plain module
   # names) and convert to the MOD_<name>-prefixed DEPENDS list. The
   # file is optional; most modules have no deps.
   set(_mod_deps "")
   set(_mod_deps_file "${PROJ_ROOT}/modules/${modname}/module_deps")
   if (EXISTS ${_mod_deps_file})
      file(STRINGS ${_mod_deps_file} _mod_raw_deps)
      foreach(_dep_name ${_mod_raw_deps})
         list(APPEND _mod_deps "MOD_${_dep_name}")
      endforeach()
   endif()

   tilck_option(MOD_${modname}
      TYPE     BOOL
      CATEGORY "Modules"
      DEFAULT  ${_mod_default}
      DEPENDS  ${_mod_deps}
      HELP     "Compile-in the ${modname} module"
   )

   unset(_mod_default)
   unset(_mod_deps)
   unset(_mod_deps_file)

   # If the module ships an options.cmake, include it now. These
   # files typically declare per-module tilck_option()s (category
   # "Modules/<name>", DEPENDS MOD_<name>) that appear as a
   # sub-menu under Modules and are hidden in mconf when the
   # module's compile-in toggle is off.
   set(_mod_opts_file "${PROJ_ROOT}/modules/${modname}/options.cmake")
   if (EXISTS ${_mod_opts_file})
      include(${_mod_opts_file})
   endif()
   unset(_mod_opts_file)

   set(MOD_HEADER_FILE ${PROJ_ROOT}/config/kernel/mod_${modname}.h)
   if (NOT EXISTS ${MOD_HEADER_FILE})
      message(FATAL_ERROR "Mod header ${MOD_HEADER_FILE} does not exist")
   endif()
   unset(MOD_HEADER_FILE)

endforeach()

# Create a list with the shared kernel opts, in order to dump it later.
# Kernel-private options are printed by the kernel CMake project.
# -------------------------------------------------------------------

list(
   APPEND kernel_opts_list

   # Various options
   ARCH
   BOARD

   # Shared non-boolean options
   TTY_COUNT
   FATPART_CLUSTER_SIZE
   PREFERRED_GFX_MODE_W
   PREFERRED_GFX_MODE_H

   # Shared boolean options ENABLED by default
   PANIC_SHOW_STACKTRACE
   DEBUG_CHECKS
   BOOTLOADER_LEGACY
   BOOTLOADER_EFI
   BOOTLOADER_U_BOOT
   BOOT_INTERACTIVE
   SERIAL_CON_IN_VIDEO_MODE
   KRN32_LIN_VADDR
   USERAPPS_busybox
   TYPICAL_DEVEL_USERAPPS

   # Shared boolean options DISABLED by default
   KERNEL_UBSAN
   TERM_BIG_SCROLL_BUF
   TEST_GCOV
   KERNEL_GCOV
   KERNEL_SYSCC
   KERNEL_FORCE_TC_ISYSTEM
   PANIC_SHOW_REGS
   BOOTLOADER_POISON_MEMORY
   WCONV
   FAT_TEST_DIR
   PS2_DO_SELFTEST
   PS2_VERBOSE_DEBUG_LOG
   INIT_REPORT_PROC_EXIT
   EFI_BOOTLOADER_DEBUG
   KERNEL_SERCON
   KERNEL_SAT
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

# Target-side per-arch package root. toolchain4 layout:
#
#     ${TCROOT}/gcc-${GCC_TC_VER}/${ARCH}/<pkg>/<ver>/
#
# The "gcc-" prefix leaves room for other compiler families in the future
# (clang/llvm slots) and the dotted version matches the rest of the
# ecosystem (vs. the underscore-mangled path used in toolchain3).
set(TCROOT_ARCH_DIR           ${TCROOT}/gcc-${GCC_TC_VER}/${ARCH})
set(TCROOT_ARCH_DIR_PARENT    ${TCROOT}/gcc-${GCC_TC_VER})

set(BUSYBOX_DIR               ${TCROOT_ARCH_DIR}/busybox/${VER_BUSYBOX})
set(NCURSES_DIR               ${TCROOT_ARCH_DIR}/ncurses)
set(ZLIB_DIR                  ${TCROOT_ARCH_DIR}/zlib/${VER_ZLIB})
set(GNUEFI_DIR                ${TCROOT_ARCH_DIR}/gnuefi/${VER_GNUEFI})
set(LIBFDT_DIR                ${TCROOT_ARCH_DIR}/dtc/${VER_DTC}/libfdt)
set(LCOV_DIR                  ${TCROOT}/noarch/lcov/${VER_LCOV})

set(BUSYBOX_CONFIG            ${BUSYBOX_DIR}/.config)

# BEGIN [ARCH-dependent paths]

   # Musl toolchains live under HOST_DIR_PORTABLE because they are 100%
   # statically-linked and libc-independent. toolchain4 layout:
   #
   #     ${HOST_DIR_PORTABLE}/gcc-${ARCH}-musl/${GCC_TC_VER}/
   #
   # The GCC version is INSIDE the package dir (not embedded in the name),
   # so a single gcc-<arch>-musl package can carry multiple versions side
   # by side.
   set(GCC_TC_ROOT            ${HOST_DIR_PORTABLE}/gcc-${ARCH}-musl/${GCC_TC_VER})
   set(GCC_TC_ROOT_i386       ${HOST_DIR_PORTABLE}/gcc-i386-musl/${GCC_TC_VER})
   set(GCC_TC_ROOT_x86_64     ${HOST_DIR_PORTABLE}/gcc-x86_64-musl/${GCC_TC_VER})
   set(GCC_TOOLCHAIN          ${GCC_TC_ROOT}/bin)
   set(GCC_TOOLCHAIN_i386     ${GCC_TC_ROOT_i386}/bin)
   set(GCC_TOOLCHAIN_x86_64   ${GCC_TC_ROOT_x86_64}/bin)

   set(GCC_TC_LIBC_DIRNAME    ${ARCH_GCC_TC}-linux-musl)
   set(GCC_TC_SYSROOT         ${GCC_TC_ROOT}/${GCC_TC_LIBC_DIRNAME})
   set(GCC_TC_ISYSTEM         ${GCC_TC_SYSROOT}/usr/include)

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

# Build the enabled-modules list for downstream consumers. The
# inter-module DEPENDS enforcement that used to live in this loop
# (reading modules/<mod>/module_deps and FATAL_ERROR'ing on unmet
# deps) is now handled uniformly by tilck_finalize_options_deps()
# via each MOD_*'s DEPENDS clause (see the MOD_* tilck_option loop
# above, which reads the same module_deps file).
foreach (mod ${modules_list})
   if (${MOD_${mod}})
      list(APPEND enabled_modules_list ${mod})
   endif()
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

   set(QEMU_ARCH_OPTS      "-device ide-hd,drive=img1,${QEMU_CHS_GEOM}")

elseif (${ARCH} STREQUAL "x86_64")

   set(QEMU_ARCH_OPTS      "-device ide-hd,drive=img1,${QEMU_CHS_GEOM}")

elseif (${ARCH} STREQUAL "riscv64")

   set(QEMU_ARCH_OPTS      "-device virtio-blk-device,drive=img1")
   set(QEMU_ARCH_OPTS      "${QEMU_ARCH_OPTS} -M virt")
   set(QEMU_ARCH_OPTS      "${QEMU_ARCH_OPTS} -kernel ${U_BOOT_BIN}")
   set(QEMU_ARCH_OPTS      "${QEMU_ARCH_OPTS} -monitor none")

endif()

# Set kernel's stack size in number of pages.
set(KERNEL_STACK_PAGES          4)

if (${ARCH_BITS} EQUAL 32)
   set(ELFHACK ${BUILD_APPS}/elfhack32)
else()
   set(ELFHACK ${BUILD_APPS}/elfhack64)
endif()

set(FATHACK ${BUILD_APPS}/fathack)

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

# Generate modules_config.h (shared module flags + variables for boot/userapps)
set(_mc "/* Auto-generated by root CMake. Do not edit. */\n\n#pragma once\n\n")
string(APPEND _mc "/* Module enable/disable flags */\n")
foreach(_mod ${modules_list})
   if(MOD_${_mod})
      string(APPEND _mc "#define MOD_${_mod} 1\n")
   else()
      string(APPEND _mc "#define MOD_${_mod} 0\n")
   endif()
endforeach()
string(APPEND _mc "\n/* Shared module config variables */\n")
string(APPEND _mc "#define TTY_COUNT              ${TTY_COUNT}\n")
macro(write_bool_define _var)
   if(${_var})
      string(APPEND _mc "#define ${_var} 1\n")
   else()
      string(APPEND _mc "#define ${_var} 0\n")
   endif()
endmacro()
write_bool_define(SERIAL_CON_IN_VIDEO_MODE)
write_bool_define(KERNEL_SERCON)
write_bool_define(TERM_BIG_SCROLL_BUF)
write_bool_define(PS2_DO_SELFTEST)
write_bool_define(PS2_VERBOSE_DEBUG_LOG)
file(WRITE "${CMAKE_BINARY_DIR}/tilck_gen_headers/modules_config.h.tmp" "${_mc}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
   "${CMAKE_BINARY_DIR}/tilck_gen_headers/modules_config.h.tmp"
   "${CMAKE_BINARY_DIR}/tilck_gen_headers/modules_config.h"
)
file(REMOVE "${CMAKE_BINARY_DIR}/tilck_gen_headers/modules_config.h.tmp")
unset(_mc)

# Include kernel's noarch sources
include(kernel/sources.cmake)

################################################################################
# Include sub-directories

add_subdirectory(scripts/build_apps)

################################################################################

# Generate config files & build scripts (after including sub-dirs)
include(other/cmake/gen_config_post.cmake)

set(BUILD_SCRIPTS_FILES_LIST "")
file(GLOB files ${GLOB_CONF_DEP} "${BUILD_SCRIPTS}/*")
foreach (f ${files})
   list(APPEND BUILD_SCRIPTS_FILES_LIST "${f}")
endforeach()
unset(files)

set(dd_opts "status=none" "conv=notrunc")

if (TYPICAL_DEVEL_USERAPPS)
   set(USERAPPS_termtest ON CACHE BOOL "Include `termtest` in fatpart")
   set(USERAPPS_fbtest ON CACHE BOOL "Include `fbtest` in fatpart")
   set(USERAPPS_filedump OFF CACHE BOOL "Include `filedump` in fatpart")
   set(USERAPPS_ncapp OFF CACHE BOOL "Include `ncapp` in fatpart")
else()
   set(USERAPPS_filedump OFF)
   set(USERAPPS_ncapp OFF)
   set(USERAPPS_termtest OFF)
   set(USERAPPS_fbtest OFF)
endif()

set(USERAPPS_play OFF CACHE BOOL "Include `play` in fatpart")
set(USERAPPS_devshell ON CACHE BOOL "Include `devshell` in fatpart")
set(USERAPPS_init ON CACHE BOOL "Include `init` in fatpart")

if (MOD_debugpanel)
   set(USERAPPS_dp ON CACHE BOOL "Include `dp` in fatpart")
endif()

set(USERAPPS_FILES_LIST "")
foreach(_app init devshell dp termtest fbtest filedump ncapp play)
   if (USERAPPS_${_app})
      list(APPEND USERAPPS_FILES_LIST ${CMAKE_BINARY_DIR}/userapps/${_app})
   endif()
endforeach()
unset(_app)

# Read extra app manifests (populated by userapps/extra after first build).
# file(GLOB CONFIGURE_DEPENDS ...) triggers a reconfigure when manifests appear
# or change, enabling incremental builds after the initial full build.
file(GLOB _extra_manifests
   CONFIGURE_DEPENDS
   "${CMAKE_BINARY_DIR}/userapps/extra/*/manifest.txt"
)
set(EXTRA_APPS_FILES_LIST "")
foreach(_manifest ${_extra_manifests})
   file(STRINGS "${_manifest}" _entries)
   list(APPEND EXTRA_APPS_FILES_LIST ${_entries})
endforeach()
list(FILTER EXTRA_APPS_FILES_LIST EXCLUDE REGEX "^$")
unset(_extra_manifests)
unset(_entries)
unset(_manifest)

################################################################################
# Generate tilck_config.cmake for ExternalProject subprojects

set(TILCK_EXPORTED_VARS
   # Project's version
   tilck_VERSION_MAJOR tilck_VERSION_MINOR tilck_VERSION_PATCH

   # Project paths
   PROJ_ROOT BUILD_DIR BUILD_APPS BUILD_SCRIPTS CMAKE_DIR

   # Architecture
   ARCH ARCH_BITS ARCH_SHORT ARCH_FAMILY
   ARCH_ELF_NAME ARCH_LD_OUTPUT ARCH_BFD ARCH_GCC_TC ARCH_GCC_FLAGS

   # Board
   BOARD BOARD_BSP

   # Toolchain paths, labels and versions
   TCROOT TCROOT_ARCH_DIR TCROOT_ARCH_DIR_PARENT
   HOST_DIR_PORTABLE HOST_DIR_DISTRO HOST_DIR
   GCC_TC_VER
   GCC_TC_ROOT GCC_TC_ROOT_i386 GCC_TC_ROOT_x86_64
   GCC_TOOLCHAIN GCC_TOOLCHAIN_i386 GCC_TOOLCHAIN_x86_64
   GCC_TC_LIBC_DIRNAME GCC_TC_SYSROOT GCC_TC_ISYSTEM

   # Package directories
   BUSYBOX_DIR NCURSES_DIR ZLIB_DIR GNUEFI_DIR LIBFDT_DIR LCOV_DIR
   BUSYBOX_CONFIG

   # Host info
   HOST_ARCH HOST_ARCH_BITS HOST_ARCH_FAMILY
   HOST_OS HOST_DISTRO HOST_CC

   # Kernel/boot memory layout
   BASE_VA KERNEL_PADDR LINEAR_MAPPING_MB KERNEL_BASE_VA KERNEL_VADDR
   KERNEL_STACK_PAGES

   # Output files
   KERNEL_FILE KERNEL_UNSTRIPPED KERNEL_SCRIPT KERNEL_BOOTPART_PATH
   BOOTLOADER_BIN_FILE EFI_x86_64_FILE EFI_ia32_FILE

   # Boot layout constants
   SECTOR_SIZE CHS_HPC CHS_SPT
   BL_ST2_DATA_SEG EARLY_BOOT_SZ STAGE3_ENTRY_OFF
   DISK_UUID MAX_BOOTLOADER_BIN_SZ_KB IMG_SZ_MB
   INITRD_SZ_MB BOOTPART_SZ_SEC
   IMG_SZ_SEC BOOT_SECTORS BOOTPART_SEC BOOTPART_LAST_SEC
   INITRD_SECTOR INITRD_SZ_SEC INITRD_LAST_SEC
   CHS_CYLS

   # Run QEMU scripts
   QEMU_CHS_GEOM QEMU_RAM_OPT QEMU_COMMON_OPTS QEMU_ARCH_OPTS

   # Derived boot values
   BL_BASE_ADDR EARLY_BOOT_SCRIPT STAGE3_SCRIPT

   # Shell / init
   DEFAULT_SHELL START_SCRIPT TILCK_DEVSHELL_PATH USERAPPS_CFLAGS

   # Shared non-boolean kernel options
   TTY_COUNT FATPART_CLUSTER_SIZE
   PREFERRED_GFX_MODE_W PREFERRED_GFX_MODE_H

   # Shared boolean kernel options
   PANIC_SHOW_STACKTRACE DEBUG_CHECKS KERNEL_64BIT_OFFT FORK_NO_COW MMAP_NO_COW
   KERNEL_SELFTESTS
   BOOTLOADER_LEGACY BOOTLOADER_EFI BOOTLOADER_U_BOOT
   SERIAL_CON_IN_VIDEO_MODE BOOT_INTERACTIVE TYPICAL_DEVEL_USERAPPS
   KRN32_LIN_VADDR
   KERNEL_UBSAN TERM_BIG_SCROLL_BUF
   KERNEL_SYSCC KERNEL_FORCE_TC_ISYSTEM
   PANIC_SHOW_REGS
   BOOTLOADER_POISON_MEMORY WCONV FAT_TEST_DIR
   PS2_DO_SELFTEST PS2_VERBOSE_DEBUG_LOG
   INIT_REPORT_PROC_EXIT
   EFI_BOOTLOADER_DEBUG
   KERNEL_SERCON KERNEL_SAT
   TEST_GCOV KERNEL_GCOV

   # USERAPPS toggles
   USERAPPS_init USERAPPS_busybox USERAPPS_devshell USERAPPS_dp
   USERAPPS_filedump USERAPPS_ncapp USERAPPS_termtest
   USERAPPS_fbtest USERAPPS_play

   # Extra apps toggles
   EXTRA_VIM EXTRA_TCC EXTRA_FBDOOM EXTRA_MICROPYTHON
   EXTRA_TREE_CMD EXTRA_LUA EXTRA_TFBLIB
   EXTRA_VIM_ENABLED EXTRA_TCC_ENABLED EXTRA_FBDOOM_ENABLED
   EXTRA_MICROPYTHON_ENABLED EXTRA_TREE_CMD_ENABLED EXTRA_LUA_ENABLED
   EXTRA_TFBLIB_ENABLED

   # Module variables
   modules_list no_arch_modules_whitelist
   ALL_MODULES_LIST ENABLED_MODULES_LIST

   # Tools
   DD FATHACK ELFHACK

   # Misc
   COVERAGE_HTML_DIR TCROOT_REL BTC_SCRIPT_REL
)

# Also export all VER_* and MOD_* variables
list(APPEND TILCK_EXPORTED_VARS ${PKG_VERSION_VARS})
foreach(_mod ${modules_list})
   list(APPEND TILCK_EXPORTED_VARS MOD_${_mod})
endforeach()

set(_config_content "# Auto-generated by the root CMake. Do not edit.\n")
foreach(_var ${TILCK_EXPORTED_VARS})
   if(DEFINED ${_var})
      string(APPEND _config_content "set(${_var} [==[${${_var}}]==])\n")
   endif()
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/tilck_config.cmake.tmp" "${_config_content}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
   "${CMAKE_BINARY_DIR}/tilck_config.cmake.tmp"
   "${CMAKE_BINARY_DIR}/tilck_config.cmake"
)
file(REMOVE "${CMAKE_BINARY_DIR}/tilck_config.cmake.tmp")
unset(_config_content)
unset(_var)

################################################################################
# ExternalProject subprojects

ExternalProject_Add(userapps
   SOURCE_DIR ${CMAKE_SOURCE_DIR}/userapps
   BINARY_DIR ${CMAKE_BINARY_DIR}/userapps
   INSTALL_COMMAND ""
   BUILD_ALWAYS 1
   BUILD_BYPRODUCTS ${USERAPPS_FILES_LIST} ${EXTRA_APPS_FILES_LIST}
   CMAKE_ARGS
      -DCMAKE_TOOLCHAIN_FILE=${CMAKE_DIR}/${ARCH}-linux-gcc.cmake
      -DGCC_TOOLCHAIN_BIN=${GCC_TOOLCHAIN}
      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      -DTILCK_CONFIG_FILE=${CMAKE_BINARY_DIR}/tilck_config.cmake
)

ExternalProject_Add_Step(userapps config_dep
   DEPENDEES download DEPENDERS configure
   DEPENDS ${CMAKE_BINARY_DIR}/tilck_config.cmake
   COMMAND ${CMAKE_COMMAND} -E true
)

# Forward all KRN_* variables from the command line to the kernel and gtests
# subprojects. Kernel-private options all use the KRN_ prefix so they can be
# set via cmake -DKRN_<option>=<value> at the root level and forwarded to both.
get_cmake_property(_all_cache_vars CACHE_VARIABLES)
set(_krn_forward_args "")
foreach(_v ${_all_cache_vars})
   if("${_v}" MATCHES "^KRN_")
      list(APPEND _krn_forward_args "-D${_v}=${${_v}}")
   endif()
endforeach()

if (NOT KERNEL_SYSCC)
   set(_kernel_tc_args
      -DCMAKE_TOOLCHAIN_FILE=${CMAKE_DIR}/${ARCH}-linux-gcc.cmake
      -DGCC_TOOLCHAIN_BIN=${GCC_TOOLCHAIN}
   )
else()
   set(_kernel_tc_args
      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
   )
endif()

ExternalProject_Add(kernel
   SOURCE_DIR ${CMAKE_SOURCE_DIR}/kernel
   BINARY_DIR ${CMAKE_BINARY_DIR}/kernel
   DEPENDS elfhack${ARCH_BITS} gen_config
   INSTALL_COMMAND ""
   BUILD_ALWAYS 1
   BUILD_BYPRODUCTS ${KERNEL_FILE} ${KERNEL_UNSTRIPPED}
   CMAKE_ARGS
      ${_kernel_tc_args}
      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      -DTILCK_CONFIG_FILE=${CMAKE_BINARY_DIR}/tilck_config.cmake
      ${_krn_forward_args}
)

ExternalProject_Add_Step(kernel config_dep
   DEPENDEES download DEPENDERS configure
   DEPENDS ${CMAKE_BINARY_DIR}/tilck_config.cmake
   COMMAND ${CMAKE_COMMAND} -E true
)

ExternalProject_Add_Step(kernel tools_dep
   DEPENDEES configure
   DEPENDERS build
   DEPENDS
      $<TARGET_FILE:elfhack${ARCH_BITS}>
      $<TARGET_FILE:gen_config>
   COMMAND ${CMAKE_COMMAND} -E true
   ALWAYS 1
)

add_dependencies(userapps kernel)

file(GLOB_RECURSE SYSROOT_FILES ${GLOB_CONF_DEP}
   "${CMAKE_SOURCE_DIR}/sysroot/*"
)
list(FILTER SYSROOT_FILES EXCLUDE REGEX "(README|\\.gitignore)$")

file(GLOB TEST_SCRIPT_FILES ${GLOB_CONF_DEP}
   "${CMAKE_SOURCE_DIR}/tests/system/scripts/*"
)

set(
   mbr_img_deps

   kernel
   ${KERNEL_FILE}

   userapps
   ${USERAPPS_FILES_LIST}
   ${EXTRA_APPS_FILES_LIST}

   fathack
   mbrhack
   ${CMAKE_BINARY_DIR}/scripts/build_apps/fathack
   ${CMAKE_BINARY_DIR}/scripts/build_apps/mbrhack
   ${SYSROOT_FILES}
   ${TEST_SCRIPT_FILES}
   ${CMAKE_BINARY_DIR}/config_fatpart
   ${BUILD_SCRIPTS_FILES_LIST}
)

if (BOOTLOADER_LEGACY)
   list(APPEND mbr_img_deps boot_legacy ${BOOTLOADER_BIN_FILE})
endif()

if (BOOTLOADER_EFI)
   list(APPEND mbr_img_deps efi_bootloader ${EFI_x86_64_FILE})

   if (${ARCH} STREQUAL "i386")
      list(APPEND mbr_img_deps ${EFI_ia32_FILE})
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
   set(MBRHACK_F ${BUILD_APPS}/mbrhack -q -f ${IMG_FILE})
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

if (BOOTLOADER_LEGACY AND BOOTLOADER_EFI)
   set(MSG "Creating a dual-bootable (legacy+efi) image file (MBR)")
elseif(BOOTLOADER_LEGACY)
   set(MSG "Creating a legacy bootable image file (MBR)")
elseif(BOOTLOADER_EFI)
   set(MSG "Creating a UEFI-only bootable image file (MBR)")
elseif(BOOTLOADER_U_BOOT)
   set(MSG "Create a U-BOOT image")
endif()


if (BOOTLOADER_LEGACY)

   ExternalProject_Add(boot_legacy
      SOURCE_DIR ${CMAKE_SOURCE_DIR}/boot/legacy
      BINARY_DIR ${CMAKE_BINARY_DIR}/boot/legacy
      DEPENDS elfhack32
      BUILD_ALWAYS 1
      BUILD_BYPRODUCTS ${BOOTLOADER_BIN_FILE}
      INSTALL_COMMAND ""
      CMAKE_ARGS
         -DCMAKE_TOOLCHAIN_FILE=${CMAKE_DIR}/i386-linux-gcc.cmake
         -DGCC_TOOLCHAIN_BIN=${GCC_TOOLCHAIN_i386}
         -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
         -DTILCK_CONFIG_FILE=${CMAKE_BINARY_DIR}/tilck_config.cmake
   )

   ExternalProject_Add_Step(boot_legacy config_dep
      DEPENDEES download DEPENDERS configure
      DEPENDS ${CMAKE_BINARY_DIR}/tilck_config.cmake
      COMMAND ${CMAKE_COMMAND} -E true
   )

   ExternalProject_Add_Step(boot_legacy elfhack_dep
      DEPENDEES configure
      DEPENDERS build
      DEPENDS $<TARGET_FILE:elfhack32>
      COMMAND ${CMAKE_COMMAND} -E true
      ALWAYS 1
   )

endif()

if (BOOTLOADER_EFI)

   ExternalProject_Add(efi_x86_64
      SOURCE_DIR ${CMAKE_SOURCE_DIR}/boot/efi/x86_64
      BINARY_DIR ${CMAKE_BINARY_DIR}/boot/efi/x86_64
      INSTALL_COMMAND ""
      BUILD_ALWAYS 1
      BUILD_BYPRODUCTS ${EFI_x86_64_FILE}
      CMAKE_ARGS
         -DCMAKE_TOOLCHAIN_FILE=${CMAKE_DIR}/x86_64-linux-gcc.cmake
         -DGCC_TOOLCHAIN_BIN=${GCC_TOOLCHAIN_x86_64}
         -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
         -DTILCK_CONFIG_FILE=${CMAKE_BINARY_DIR}/tilck_config.cmake
   )

   ExternalProject_Add_Step(efi_x86_64 config_dep
      DEPENDEES download DEPENDERS configure
      DEPENDS ${CMAKE_BINARY_DIR}/tilck_config.cmake
      COMMAND ${CMAKE_COMMAND} -E true
   )

   set(efi_bootloader_deps efi_x86_64)

   if (${ARCH} STREQUAL "i386")

      ExternalProject_Add(efi_ia32
         SOURCE_DIR ${CMAKE_SOURCE_DIR}/boot/efi/ia32
         BINARY_DIR ${CMAKE_BINARY_DIR}/boot/efi/ia32
         INSTALL_COMMAND ""
         BUILD_ALWAYS 1
         BUILD_BYPRODUCTS ${EFI_ia32_FILE}
         CMAKE_ARGS
            -DCMAKE_TOOLCHAIN_FILE=${CMAKE_DIR}/i386-linux-gcc.cmake
            -DGCC_TOOLCHAIN_BIN=${GCC_TOOLCHAIN_i386}
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DTILCK_CONFIG_FILE=${CMAKE_BINARY_DIR}/tilck_config.cmake
      )

      ExternalProject_Add_Step(efi_ia32 config_dep
         DEPENDEES download DEPENDERS configure
         DEPENDS ${CMAKE_BINARY_DIR}/tilck_config.cmake
         COMMAND ${CMAKE_COMMAND} -E true
      )

      list(APPEND efi_bootloader_deps efi_ia32)
   endif()

   add_custom_target(
      efi_bootloader
      DEPENDS ${efi_bootloader_deps}
   )

endif()

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
         ${MBRHACK} add 0xef ${BOOTPART_SEC} +${BOOTPART_SZ_SEC}
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
         "${MSG}"
      VERBATIM
   )
elseif (BOOTLOADER_EFI)
   add_custom_command(
      OUTPUT
         ${IMG_FILE}
      COMMAND
         ${CREATE_EMPTY_IMG} ${IMG_FILE} ${IMG_SZ_MB}
      COMMAND
         ${MBRHACK} mbr
      COMMAND
         ${MBRHACK_F} bpb ${MBRHACK_BPB}
      COMMAND
         ${MBRHACK} add 0xef ${BOOTPART_SEC} +${BOOTPART_SZ_SEC}
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
         "${MSG}"
      VERBATIM
   )
elseif (BOOTLOADER_U_BOOT)
   add_custom_command(
      OUTPUT
         ${IMG_FILE}
      COMMAND
         ${CREATE_EMPTY_IMG} ${IMG_FILE} ${IMG_SZ_MB}
      COMMAND
         ${MBRHACK} mbr
      COMMAND
         ${MBRHACK_F} bpb ${MBRHACK_BPB}
      COMMAND
         ${MBRHACK} add 0xef ${BOOTPART_SEC} +${BOOTPART_SZ_SEC}
      COMMAND
         ${MBRHACK} add 0x0e ${INITRD_SECTOR} +${INITRD_SZ_MB}M
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
         "${MSG}"
      VERBATIM
   )
   unset(MKIMAGE)
endif()

# [begin] Unset the convenience variables
   unset(MBRHACK_BPB)
   unset(CREATE_EMPTY_IMG)
   unset(MBRHACK)
# [end]

add_custom_target(

   tilck_image ALL

   DEPENDS
      ${IMG_FILE}
)

set(GTEST_INSTALL_DIR ${HOST_DIR}/gtest/${VER_GTEST}/install)
set(GTEST_BTC_COMMAND "host_gtest")

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

if (EXISTS ${GTEST_INSTALL_DIR})

   if (APPLE)
      set(_gtests_tool_deps machohack)
   else()
      set(_gtests_tool_deps elfhack32 elfhack64)
   endif()

   ExternalProject_Add(gtests
      SOURCE_DIR ${CMAKE_SOURCE_DIR}/tests/unit
      BINARY_DIR ${CMAKE_BINARY_DIR}/tests/unit
      DEPENDS
         test_fatpart_target
         gen_config ${_gtests_tool_deps}
      EXCLUDE_FROM_ALL TRUE
      INSTALL_COMMAND ""
      BUILD_ALWAYS 1
      BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/gtests
      CMAKE_ARGS
         -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
         -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
         -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
         -DTILCK_CONFIG_FILE=${CMAKE_BINARY_DIR}/tilck_config.cmake
         ${_krn_forward_args}
   )

   ExternalProject_Add_Step(gtests config_dep
      DEPENDEES download DEPENDERS configure
      DEPENDS ${CMAKE_BINARY_DIR}/tilck_config.cmake
      COMMAND ${CMAKE_COMMAND} -E true
   )

   ExternalProject_Add_Step(gtests tools_dep
      DEPENDEES configure
      DEPENDERS build
      DEPENDS
         $<TARGET_FILE:gen_config>
      COMMAND ${CMAKE_COMMAND} -E true
      ALWAYS 1
   )

else()
   no_googletest_lib_fake_error_target()
endif()

# Enforce deferred BOOL DEPENDS checks now that every tilck_option()
# has run (including the dynamic MOD_* loop and any ExternalProject
# sub-includes). This replaces the former CMakeLists.txt loop that
# read modules/<mod>/module_deps and FATAL_ERROR'd directly — the
# same constraint is now expressed via each MOD_*'s DEPENDS clause.
tilck_finalize_options_deps()

# Schema-validate the tilck_option() sidecar. Catches bad metadata
# (unknown types, missing fields, ENUM default not in STRINGS, etc.)
# at CMake configure time instead of surfacing only when someone runs
# scripts/run_config. An empty sidecar (no tilck_option() migrated
# yet) validates cleanly.
execute_process(
   COMMAND
      ${CMAKE_COMMAND} -E env python3
      ${CMAKE_SOURCE_DIR}/scripts/dev/configurator/gen_kconfig.py
      --sidecar ${CMAKE_BINARY_DIR}/tilck_options.json
      --validate
   RESULT_VARIABLE _tilck_opts_validate_rc
   OUTPUT_VARIABLE _tilck_opts_validate_out
   ERROR_VARIABLE  _tilck_opts_validate_err
)
if (NOT _tilck_opts_validate_rc EQUAL 0)
   message(FATAL_ERROR
      "tilck_options.json failed validation:\n${_tilck_opts_validate_err}"
   )
endif()
