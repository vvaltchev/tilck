/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#define DMA8_SINGLE_CH_MASK_REG    0x0A
#define DMA16_SINGLE_CH_MASK_REG   0xD4
#define DMA8_MODE_REG              0x0B
#define DMA16_MODE_REG             0xD6
#define DMA8_RST_FLIP_FLOP         0x0C
#define DMA16_RST_FLIP_FLOP        0xD8


/* Flags for DMA8_MODE_REG */

#define DMA_DEMAND_MODE    0b00000000  /* Bits 7:6 */
#define DMA_SINGLE_MODE    0b01000000  /* Bits 7:6 */
#define DMA_BLOCK_MODE     0b10000000  /* Bits 7:6 */
#define DMA_CASCADE_MODE   0b11000000  /* Bits 7:6 */

#define DMA_ADDR_INC       0b00000000  /* Bit 5 */
#define DMA_ADDR_DEC       0b00100000  /* Bit 5 */

#define DMA_SINGLE_CYCLE   0b00000000  /* Bit 4 */
#define DMA_AUTO_INIT      0b00010000  /* Bit 4 */

#define DMA_VERIFY_TX      0b00000000  /* Bits 3:2 */
#define DMA_WRITE_TX       0b00000100  /* Bits 3:2 */
#define DMA_READ_TX        0b00001000  /* Bits 3:2 */

/* DMA_CHANNEL_0 is unusable */
#define DMA_CHANNEL_1      0b00000001  /* Bits 1:0 */
#define DMA_CHANNEL_2      0b00000010  /* Bits 1:0 */
#define DMA_CHANNEL_3      0b00000011  /* Bits 1:0 */

/* DMA_CHANNEL_4 is unusable */
#define DMA_CHANNEL_5      0b00000001  /* Bits 1:0 */
#define DMA_CHANNEL_6      0b00000010  /* Bits 1:0 */
#define DMA_CHANNEL_7      0b00000011  /* Bits 1:0 */

/* Flags for SINGLE_CH_MASK_REG */
#define DMA_MASK_CHANNEL   0b00000100

/* DMA channels "page" address registers */

/* DMA_CHANNEL_0 is unusable */
#define DMA_CHANNEL_1_PAGE_REG   0x83
#define DMA_CHANNEL_2_PAGE_REG   0x81
#define DMA_CHANNEL_3_PAGE_REG   0x82
/* DMA_CHANNEL_4 is unusable */
#define DMA_CHANNEL_5_PAGE_REG   0x8B
#define DMA_CHANNEL_6_PAGE_REG   0x89
#define DMA_CHANNEL_7_PAGE_REG   0x8A

/* DMA start/count registers */
#define DMA_CHANNEL_1_START_REG  0x02
#define DMA_CHANNEL_1_COUNT_REG  0x03

#define DMA_CHANNEL_2_START_REG  0x04
#define DMA_CHANNEL_2_COUNT_REG  0x05

#define DMA_CHANNEL_3_START_REG  0x06
#define DMA_CHANNEL_3_COUNT_REG  0x07

#define DMA_CHANNEL_5_START_REG  0xC4
#define DMA_CHANNEL_5_COUNT_REG  0xC6

#define DMA_CHANNEL_6_START_REG  0xC8
#define DMA_CHANNEL_6_COUNT_REG  0xCA

#define DMA_CHANNEL_7_START_REG  0xCC
#define DMA_CHANNEL_7_COUNT_REG  0xCE
