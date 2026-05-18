/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/datetime.h>

/*
 * PIT crystal frequency.
 *
 * The 8253/8254 PIT is clocked by the legacy PC's 14.31818 MHz
 * oscillator divided by 12. That 14.31818 MHz number is itself a
 * derived constant -- four times the NTSC color subcarrier of
 * exactly 315/88 MHz -- so the PIT frequency in closed form is:
 *
 *     PIT_FREQ = (315/88 MHz) * 4 / 12
 *              = (105/88) MHz
 *              = 1.193181818... MHz   (the .818 repeats)
 *
 * Older code used the integer-Hz approximation `1193182` (rounded
 * up from 1193181.818); that's 0.15 ppm too high, ~13 ms/day of
 * software-induced drift on top of whatever the real crystal does.
 * Storing the constant in millihertz captures the .818 fraction
 * exactly to the precision we care about: 1,193,181,818 mHz is
 * the rounded value of 105/88 * 10^9, and the residual error is
 * 0.000018 ppm -- under 2 microseconds per day.
 *
 * u32 has room (max ~4.29e9, we use ~1.19e9). Products of
 * PIT_FREQ_MHZ * divisor up to (4.29e9 * 65535) require u64; that's
 * the only place the wider math kicks in, in hw_timer_setup().
 */
#define PIT_FREQ_MHZ       1193181818u   /* PIT crystal in millihertz */

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

#define PIT_ACC_LO      0b00010000   // access mode: lobyte only
#define PIT_ACC_HI      0b00100000   // access mode: hibyte only
#define PIT_ACC_LOHI    0b00110000   // access mode: lobyte/hibyte

#define PIT_CH0         0b00000000   // select channel 0
#define PIT_CH1         0b01000000   // select channel 1
#define PIT_CH2         0b10000000   // select channel 2

#define PIT_READ_BACK   0b11000000   // read-back command (8254 only)

/*
 * Configure the PIT to fire IRQs at as close to `hz` as the
 * hardware allows, and report back the timing parameters the caller
 * needs to keep `__time_ns` exact to the nanosecond.
 *
 * The two parts of the problem are independent:
 *
 *  1. Picking the PIT divisor. The PIT counts down a 16-bit counter
 *     at PIT_FREQ. To get `hz` IRQs per second we want
 *     divisor = PIT_FREQ / hz, but hz doesn't generally divide
 *     PIT_FREQ evenly. We pick the integer divisor closest to the
 *     ideal -- round-nearest, not floor (the old code's choice).
 *     With the default hz=250 the ideal is 4772.728, round-nearest
 *     picks 4773, giving an actual IRQ rate of 1193181.818/4773 ≈
 *     249.986 Hz: about 57 ppm slow. Floor would have given 4772
 *     and 250.038 Hz: 152 ppm fast. Both errors are absorbed by the
 *     fractional-ns accumulator below; round-nearest just makes the
 *     residue smaller in absolute terms.
 *
 *     This is a hardware limit: the IRQ rate is exactly
 *     PIT_FREQ / divisor and can't be made literally equal to
 *     `hz` for arbitrary hz unless hz happens to divide PIT_FREQ
 *     evenly (which 250 doesn't).
 *
 *  2. Reporting back ns-per-IRQ accurately. The "real" ns per IRQ
 *     is the ratio
 *
 *               divisor * TS_SCALE
 *        ns = ----------------------
 *                  PIT_FREQ
 *
 *     which is rarely an integer. The old code returned just
 *     `floor(...)` and let the caller add that to __time_ns each
 *     IRQ; the truncated fraction (sub-ns per IRQ, summed at the
 *     IRQ rate) accumulates to tens of ms per day of wall-clock
 *     lag. The caller now gets the integer part plus a
 *     (numerator, denominator) for the residue so it can run a
 *     Bresenham-style accumulator and "spend" the leftover
 *     fraction as a +1 ns every N IRQs.
 *
 *     With the residue tracked, __time_ns over any long interval
 *     converges to the exact divisor/PIT_FREQ ratio, scaled to ns.
 *     Software drift goes away. Whatever drift remains is the
 *     hardware crystal's deviation from PIT_FREQ_MHZ -- and that's
 *     what the kernel/datetime.c RTC-anchored compensation is for.
 *
 * `interval` is in 1/TS_SCALE-second units (so 4_000_000 ns at the
 * default TS_SCALE=1e9 and HZ=250). It exists mostly to make the
 * caller's intent legible -- under the hood we work with `hz`.
 */
void hw_timer_setup(u32 interval, struct hw_timer_info *out)
{
   const u32 hz = TS_SCALE / interval;
   u32 divisor;
   u64 num;

   ASSERT(IN_RANGE_INC(hz, 18, 1000));
   ASSERT(out != NULL);

   /*
    * Round-nearest divisor in milliHz units. The "+ 500*hz" is the
    * half-step bias for round-to-nearest in the scaled space:
    * dividing by (1000 * hz) gives the divisor in raw PIT counts.
    */
   divisor = (PIT_FREQ_MHZ + 500u * hz) / (1000u * hz);

   /*
    * ns per tick = divisor * TS_SCALE / PIT_FREQ. Working in
    * milliHz means a leading * 1000 in the numerator and PIT_FREQ
    * appears as PIT_FREQ_MHZ in the denominator. u64 is needed
    * for the product (worst case: 65535 * 1e9 * 1000 ≈ 6.6e16).
    */
   num = (u64)divisor * TS_SCALE * 1000u;

   out->ns_per_tick   = (u32)(num / PIT_FREQ_MHZ);
   out->frac_per_tick = (u32)(num % PIT_FREQ_MHZ);
   out->frac_denom    = PIT_FREQ_MHZ;

   ASSERT(out->ns_per_tick < UINT32_MAX);

   outb(PIT_CMD_PORT, PIT_MODE_BIN | PIT_MODE_2 | PIT_ACC_LOHI | PIT_CH0);
   outb(PIT_CH0_PORT, divisor & 0xff);            /* Set low byte of divisor */
   outb(PIT_CH0_PORT, (divisor >> 8) & 0xff);     /* Set high byte of divisor */
}
