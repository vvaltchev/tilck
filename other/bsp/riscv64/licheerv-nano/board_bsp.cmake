# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

# Full SoC codename for this board.
set(CODENAME "sg2002_licheervnano_sd")
set(UBOOT_BUILD ${TCROOT_ARCH_DIR}/bootloader/u-boot-2021.10/build)

macro(get_cvi_board_memmap)

   file(
      STRINGS ${BOARD_MEMMAP_FILE} ${ARGV0}_STR
      REGEX "^CVIMMAP_${ARGV0}=.*$"
   )

   message(STATUS "${ARGV0}_STR orig: ${${ARGV0}_STR}")

   string(
      REGEX REPLACE "^CVIMMAP_${ARGV0}=0x(.*)$" "\\1"
      ${ARGV0}_STR ${${ARGV0}_STR}
   )

   message(STATUS "${ARGV0}_STR replace: ${${ARGV0}_STR}")

   set(${ARGV0} 0x${${ARGV0}_STR})

   message(STATUS "${ARGV0}: ${${ARGV0}}")

endmacro()

set(
   BOARD_MEMMAP_FILE
   ${TCROOT_ARCH_DIR}/bootloader/build/output/${CODENAME}/cvi_board_memmap.conf
)

# Get the uimage address, which will be used for boot scripts
get_cvi_board_memmap(UIMAG_ADDR)

set(
   BOARD_BSP_BOOTLOADER
   ${TCROOT_ARCH_DIR}/bootloader/install/soc_${CODENAME}/fip.bin
)

set(BOARD_BSP_MKIMAGE
   ${UBOOT_BUILD}/${CODENAME}/tools/mkimage
)

set(
   BOARD_DTB_FILE
   ${UBOOT_BUILD}/${CODENAME}/arch/riscv/dts/${CODENAME}.dtb
)

set(KERNEL_PADDR                  0x80200000)

# Parameters required by boot script of u-boot
math(EXPR KERNEL_ENTRY "${KERNEL_PADDR} + 0x1000" OUTPUT_FORMAT HEXADECIMAL)
math(EXPR KERNEL_LOAD "${UIMAG_ADDR} - 0x800000" OUTPUT_FORMAT HEXADECIMAL)

# licheerv-nano do not have a rtc
set(KRN_CLOCK_DRIFT_COMP OFF CACHE BOOL
    "Compensate periodically for the clock drift in the system time" FORCE)

