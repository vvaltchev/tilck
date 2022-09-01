# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

set(EARLY_BOOT_SCRIPT ${CMAKE_BINARY_DIR}/boot/legacy/early_boot_script.ld)
set(STAGE3_SCRIPT ${CMAKE_BINARY_DIR}/boot/legacy/stage3/linker_script.ld)
set(KERNEL_SCRIPT ${CMAKE_BINARY_DIR}/kernel/arch/${ARCH}/linker_script.ld)
set(MUSL_GCC ${CMAKE_BINARY_DIR}/scripts/musl-gcc)
set(MUSL_GXX ${CMAKE_BINARY_DIR}/scripts/musl-g++)

hex2dec(${BL_ST2_DATA_SEG} BL_ST2_DATA_SEG_DEC)

math(EXPR BL_BASE_ADDR_DEC
      "${BL_ST2_DATA_SEG_DEC} * 16 + ${EARLY_BOOT_SZ} + ${STAGE3_ENTRY_OFF}")

dec2hex(${BL_BASE_ADDR_DEC} BL_BASE_ADDR)

file(GLOB config_glob ${GLOB_CONF_DEP} "${CMAKE_SOURCE_DIR}/config/*.h")

foreach(config_path ${config_glob})

   get_filename_component(config_name ${config_path} NAME_WE)

   smart_config_file(
      ${config_path}
      ${CMAKE_BINARY_DIR}/tilck_gen_headers/${config_name}.h
   )

endforeach()

smart_config_file(
   ${CMAKE_SOURCE_DIR}/config/modules_list.h
   ${CMAKE_BINARY_DIR}/tilck_gen_headers/modules_list.h
)

smart_config_file(
   ${CMAKE_SOURCE_DIR}/config/config_init.h
   ${CMAKE_BINARY_DIR}/tilck_gen_headers/config_init.h
)

smart_config_file(
   ${CMAKE_SOURCE_DIR}/boot/legacy/early_boot_script.ld
   ${EARLY_BOOT_SCRIPT}
)

smart_config_file(
   ${CMAKE_SOURCE_DIR}/kernel/arch/${ARCH}/linker_script.ld
   ${KERNEL_SCRIPT}
)

smart_config_file(
   ${CMAKE_SOURCE_DIR}/other/musl-gcc
   ${MUSL_GCC}
)

smart_config_file(
   ${CMAKE_SOURCE_DIR}/other/musl-g++
   ${MUSL_GXX}
)

smart_config_file(
   ${CMAKE_SOURCE_DIR}/tests/runners/single_test_run
   ${CMAKE_BINARY_DIR}/st/single_test_run
)

smart_config_file(
   ${CMAKE_SOURCE_DIR}/tests/runners/run_all_tests
   ${CMAKE_BINARY_DIR}/st/run_all_tests
)

smart_config_file(
   ${CMAKE_SOURCE_DIR}/tests/runners/run_interactive_test
   ${CMAKE_BINARY_DIR}/st/run_interactive_test
)

smart_config_file(
   ${CMAKE_SOURCE_DIR}/other/cmake/config_fatpart
   ${CMAKE_BINARY_DIR}/config_fatpart
)

smart_config_file(
   ${CMAKE_SOURCE_DIR}/other/tilck_unstripped-gdb.py
   ${CMAKE_BINARY_DIR}/tilck_unstripped-gdb.py
)

smart_config_file(
   ${CMAKE_SOURCE_DIR}/scripts/templates/weaken_syms
   ${CMAKE_BINARY_DIR}/scripts/weaken_syms
)

# Run qemu scripts

list(
   APPEND run_qemu_files

   run_nokvm_qemu
   run_qemu
   run_multiboot_nokvm_qemu
   run_multiboot_qemu
   run_efi_nokvm_qemu64
   run_efi_qemu64
   debug_run_qemu
)

if (${ARCH} STREQUAL "i386")

   list(
      APPEND run_qemu_files

      run_efi_nokvm_qemu32
      run_efi_qemu32
   )

endif()

foreach(script_file ${run_qemu_files})
   smart_config_file(
      ${CMAKE_SOURCE_DIR}/scripts/templates/qemu/${script_file}
      ${CMAKE_BINARY_DIR}/${script_file}
   )
endforeach()

include_directories(${CMAKE_BINARY_DIR})
