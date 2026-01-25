# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

project(kernel LANGUAGES C CXX ASM)

include(CheckCCompilerFlag)
set(GLOB_CONF_DEP CONFIGURE_DEPENDS)
set(BUILD_SHARED_LIBRARIES off)           # disable shared libs globally
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)    # disable -rdynamic globally
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS)  # disable -rdynamic globally

include(${PROJ_ROOT}/other/cmake/compiler_flags.cmake)
include(${PROJ_ROOT}/other/cmake/utils.cmake)
include(${PROJ_ROOT}/other/cmake/errors.cmake)
include(${PROJ_ROOT}/kernel/sources.cmake)

include_directories(${PROJ_ROOT}/include)
include_directories(${PROJ_ROOT}/include/system_headers)
include_directories(${TCROOT}/noarch/acpica/${VER_ACPICA})
include_directories(${BUILD_DIR})

# Include the funcs for building the kernel modules, used both by the kernel
# and the gtests targets.

include(${PROJ_ROOT}/other/cmake/wrapped_syms.cmake)
include(${PROJ_ROOT}/other/cmake/build_modules.cmake)

set(ELFHACK ${BUILD_APPS}/elfhack${ARCH_BITS})



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

file(GLOB modules ${GLOB_CONF_DEP} "${PROJ_ROOT}/modules/*")

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

   set(MOD_HEADER_FILE ${PROJ_ROOT}/config/mod_${modname}.h)
   if (NOT EXISTS ${MOD_HEADER_FILE})
      message(FATAL_ERROR "Mod header ${MOD_HEADER_FILE} does not exist")
   endif()
   unset(MOD_HEADER_FILE)

endforeach()

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

   set(deps_file "${PROJ_ROOT}/modules/${mod}/module_deps")

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

# Generate all the per-module config files, from their templates.
file(GLOB config_glob ${GLOB_CONF_DEP} "${PROJ_ROOT}/config/mod_*.h")

foreach(config_path ${config_glob})

   get_filename_component(config_name ${config_path} NAME_WE)
   smart_config_file(
      ${config_path}
      ${BUILD_DIR}/tilck_gen_headers/${config_name}.h
   )

endforeach()

# Generate the modules_list.h file, that's needed by the kernel's .tilck_info
# section.
smart_config_file(
   ${PROJ_ROOT}/config/modules_list.h
   ${BUILD_DIR}/tilck_gen_headers/modules_list.h
)

unset(enabled_modules_list)
add_subdirectory(arch)
