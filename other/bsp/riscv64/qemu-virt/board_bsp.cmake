# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.22)

set(BOARD_BSP_BOOTLOADER          ${TCROOT}/${ARCH}/u-boot/u-boot.bin)
set(BOARD_BSP_MKIMAGE          ${TCROOT}/${ARCH}/u-boot/tools/mkimage)

set(KERNEL_PADDR                  0x80200000)  # Default

# Parameters required by boot script of u-boot
math(EXPR KERNEL_ENTRY "${KERNEL_PADDR} + 0x1000"
      OUTPUT_FORMAT HEXADECIMAL)
math(EXPR KERNEL_LOAD "${KERNEL_PADDR} + 0x800000"
      OUTPUT_FORMAT HEXADECIMAL)
math(EXPR INITRD_LOAD "${KERNEL_PADDR} + 0x1400000"
      OUTPUT_FORMAT HEXADECIMAL)

