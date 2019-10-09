/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/timer.h>

#define PIT_CMD_PORT          0x43
#define PIT_CH0_PORT          0x40
#define PIT_CH1_PORT          0x41
#define PIT_CH2_PORT          0x42

#define PIT_MODE_BIN    0b00000000
#define PIT_MODE_BCD    0b00000001

#define PIT_MODE_0      0b00000000   // interrupt on terminal count
#define PIT_MODE_1      0b00000010   // hardware re-triggerable one-shot
#define PIT_MODE_2      0b00000100   // rate generator
#define PIT_MODE_3      0b00000110   // square wave generator
#define PIT_MODE_4      0b00001000   // software triggered strobe
#define PIT_MODE_5      0b00001010   // hardware triggered strobe

#define PIT_LATCH_CMD   0b00000000   // latch count value command
#define PIT_ACC_LO      0b00010000   // access mode: lobyte only
#define PIT_ACC_HI      0b00100000   // access mode: hibyte only
#define PIT_ACC_LOHI    0b00110000   // access mode: lobyte/hibyte

#define PIT_CH0         0b00000000   // select channel 0
#define PIT_CH1         0b01000000   // select channel 1
#define PIT_CH2         0b10000000   // select channel 2

#define PIT_READ_BACK   0b11000000   // read-back command (8254 only)

void timer_set_freq(u32 hz)
{
   ASSERT(IN_RANGE_INC(hz, 18, 1000));

   u32 divisor = 1193180 / hz;

   outb(PIT_CMD_PORT, PIT_MODE_BIN | PIT_MODE_3 | PIT_ACC_LOHI | PIT_CH0);
   outb(PIT_CH0_PORT, divisor & 0xff);            /* Set low byte of divisor */
   outb(PIT_CH0_PORT, (divisor >> 8) & 0xff);     /* Set high byte of divisor */
}
