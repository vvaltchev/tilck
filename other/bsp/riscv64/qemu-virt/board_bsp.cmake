# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

set(UBOOT_DIR                     ${TCROOT_ARCH_DIR}/uboot/${VER_UBOOT})
set(BOARD_BSP_BOOTLOADER          ${UBOOT_DIR}/u-boot.bin)
set(BOARD_BSP_MKIMAGE             ${UBOOT_DIR}/tools/mkimage)

set(KERNEL_PADDR                  0x80200000)  # Default

# Parameters required by boot script of u-boot
math(EXPR KERNEL_ENTRY "${KERNEL_PADDR} + 0x1000"
      OUTPUT_FORMAT HEXADECIMAL)
math(EXPR KERNEL_LOAD "${KERNEL_PADDR} + 0x800000"
      OUTPUT_FORMAT HEXADECIMAL)
math(EXPR INITRD_LOAD "${KERNEL_PADDR} + 0x1400000"
      OUTPUT_FORMAT HEXADECIMAL)

