
####################################
# Util funcs
####################################

function(h2d_char hc dec_out)

   if ("${hc}" MATCHES "[0-9]")
      set(${dec_out} ${hc} PARENT_SCOPE)
   elseif ("${hc}" STREQUAL "a")
      set(${dec_out} 10 PARENT_SCOPE)
   elseif("${hc}" STREQUAL "b")
      set(${dec_out} 11 PARENT_SCOPE)
   elseif("${hc}" STREQUAL "c")
      set(${dec_out} 12 PARENT_SCOPE)
   elseif("${hc}" STREQUAL "d")
      set(${dec_out} 13 PARENT_SCOPE)
   elseif("${hc}" STREQUAL "e")
      set(${dec_out} 14 PARENT_SCOPE)
   elseif("${hc}" STREQUAL "f")
      set(${dec_out} 15 PARENT_SCOPE)
   else()
      message(FATAL_ERROR "Invalid digit '${hc}'")
   endif()

endfunction()

function(hex2dec val out)

   if (NOT "${val}" MATCHES "^0x[0-9a-f]+$")
      message(FATAL_ERROR "Invalid hex number '${val}'")
   endif()

   string(LENGTH "${val}" len)
   math(EXPR len "${len} - 2")
   string(SUBSTRING "${val}" 2 ${len} val) # skip the "0x" prefix


   set(res 0)
   set(mul 1)
   math(EXPR len "${len} - 1")

   foreach (i RANGE "${len}")

      math(EXPR j "${len} - ${i}")
      string(SUBSTRING "${val}" ${j} 1 c)

      h2d_char(${c} c)

      math(EXPR res "${res} + ${c} * ${mul}")
      math(EXPR mul "${mul} * 16")

      if (${res} LESS 0)
         string(CONCAT err "The hex number 0x${val} is too big "
                           "for CMake: it does not fit in a signed 32-bit int")

         message(FATAL_ERROR "${err}")
      endif()

   endforeach()

   set(${out} ${res} PARENT_SCOPE)

endfunction()


function(dec2hex val out)

   if (NOT "${val}" MATCHES "^[0-9]+$")
      message(FATAL_ERROR "Invalid decimal number '${val}'")
   endif()

   set(hex_chars "0123456789abcdef")
   set(res "")

   if (${val} EQUAL 0)
      set(${out} "0x0" PARENT_SCOPE)
   else()

      while (${val} GREATER 0)

         math (EXPR q "${val} % 16")
         string(SUBSTRING "${hex_chars}" ${q} 1 c)
         set(res "${c}${res}")

         math (EXPR val "${val} / 16")
      endwhile()

      set(${out} "0x${res}" PARENT_SCOPE)

   endif()
endfunction()

################################################################################

set(EARLY_BOOT_SCRIPT ${PROJECT_BINARY_DIR}/bootloader/early_boot_script.ld)
set(STAGE3_SCRIPT ${PROJECT_BINARY_DIR}/bootloader/elf_stage3_script.ld)
set(KERNEL_SCRIPT ${CMAKE_SOURCE_DIR}/kernel/arch/${ARCH}/linker_script.ld)

hex2dec(${BL_ST2_DATA_SEG} BL_ST2_DATA_SEG_DEC)

math(EXPR BL_BASE_ADDR_DEC
      "${BL_ST2_DATA_SEG_DEC} * 16 + ${EARLY_BOOT_SZ} + ${STAGE3_ENTRY_OFF}")

dec2hex(${BL_BASE_ADDR_DEC} BL_BASE_ADDR)

configure_file(
   ${PROJECT_SOURCE_DIR}/include/common/generated_config_template.h
   ${PROJECT_BINARY_DIR}/generated_config.h
)

configure_file(
   ${PROJECT_SOURCE_DIR}/bootloader/early_boot_script.ld
   ${EARLY_BOOT_SCRIPT}
)

configure_file(
   ${PROJECT_SOURCE_DIR}/bootloader/elf_stage3_script.ld
   ${STAGE3_SCRIPT}
)

# Run qemu scripts

configure_file(
   ${PROJECT_SOURCE_DIR}/scripts/run_nokvm_qemu
   ${PROJECT_BINARY_DIR}/run_nokvm_qemu
)

configure_file(
   ${PROJECT_SOURCE_DIR}/scripts/run_qemu
   ${PROJECT_BINARY_DIR}/run_qemu
)

configure_file(
   ${PROJECT_SOURCE_DIR}/scripts/run_nokvm_qemu_with_usbdisk
   ${PROJECT_BINARY_DIR}/run_nokvm_qemu_with_usbdisk
)

configure_file(
   ${PROJECT_SOURCE_DIR}/scripts/run_multiboot_nokvm_qemu
   ${PROJECT_BINARY_DIR}/run_multiboot_nokvm_qemu
)

configure_file(
   ${PROJECT_SOURCE_DIR}/scripts/run_multiboot_qemu
   ${PROJECT_BINARY_DIR}/run_multiboot_qemu
)

configure_file(
   ${PROJECT_SOURCE_DIR}/scripts/run_efi_nokvm_qemu32
   ${PROJECT_BINARY_DIR}/run_efi_nokvm_qemu32
)

configure_file(
   ${PROJECT_SOURCE_DIR}/scripts/run_efi_qemu32
   ${PROJECT_BINARY_DIR}/run_efi_qemu32
)

configure_file(
   ${PROJECT_SOURCE_DIR}/scripts/run_efi_nokvm_qemu64
   ${PROJECT_BINARY_DIR}/run_efi_nokvm_qemu64
)

configure_file(
   ${PROJECT_SOURCE_DIR}/scripts/run_efi_qemu64
   ${PROJECT_BINARY_DIR}/run_efi_qemu64
)

configure_file(
   ${PROJECT_SOURCE_DIR}/scripts/debug_run_qemu
   ${PROJECT_BINARY_DIR}/debug_run_qemu
)

include_directories(${CMAKE_BINARY_DIR})
