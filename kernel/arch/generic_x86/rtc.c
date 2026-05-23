/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/datetime.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/worker_thread.h>

#define CMOS_CONTROL_PORT                 0x70
#define CMOS_DATA_PORT                    0x71

#define REG_SECONDS                       0x00
#define REG_MINUTES                       0x02
#define REG_HOURS                         0x04
#define REG_WEEKDAY                       0x06
#define REG_DAY                           0x07
#define REG_MONTH                         0x08
#define REG_YEAR                          0x09

#define REG_STATUS_REG_A                  0x0A
#define REG_STATUS_REG_B                  0x0B
#define REG_STATUS_REG_C                  0x0C

#define STATUS_REG_A_UPDATE_IN_PROGRESS   0x80

/* Register B (control) bits */
#define REG_B_UIE                        (1u << 4)   /* update-ended IRQ enable */

/* Register C (interrupt flags, read-clear) bits */
#define REG_C_UF                         (1u << 4)   /* update-ended flag */
#define REG_C_AF                         (1u << 5)   /* alarm flag */
#define REG_C_PF                         (1u << 6)   /* periodic flag */
#define REG_C_IRQF                       (1u << 7)   /* any of the above */

static inline u8 bcd_to_dec(u8 bcd)
{
   return ((bcd & 0xf0) >> 1) + ((bcd & 0xf0) >> 3) + (bcd & 0xf);
}

static inline u32 cmos_read_reg(u8 reg)
{
   u8 NMI_disable_bit = 0; // temporary
   outb(CMOS_CONTROL_PORT, (u8)(NMI_disable_bit << 7) | reg);
   return inb(CMOS_DATA_PORT);
}

static inline void cmos_write_reg(u8 reg, u8 value)
{
   u8 NMI_disable_bit = 0; // temporary
   outb(CMOS_CONTROL_PORT, (u8)(NMI_disable_bit << 7) | reg);
   outb(CMOS_DATA_PORT, value);
}

static inline bool cmos_is_update_in_progress(void)
{
   return cmos_read_reg(REG_STATUS_REG_A) & STATUS_REG_A_UPDATE_IN_PROGRESS;
}

static void cmod_read_datetime_raw(struct datetime *d)
{
   d->sec = (u8) cmos_read_reg(REG_SECONDS);
   d->min = (u8) cmos_read_reg(REG_MINUTES);
   d->hour = (u8) cmos_read_reg(REG_HOURS);
   d->__pad = 0;

   d->day = (u8) cmos_read_reg(REG_DAY);
   d->month = (u8) cmos_read_reg(REG_MONTH);
   d->year = (u16) cmos_read_reg(REG_YEAR);
}

void hw_read_clock_cmos(struct datetime *out)
{
   struct datetime d, dlast;
   u32 reg_b;
   bool use_24h;
   bool use_binary;
   bool hour_pm_bit;

   reg_b = cmos_read_reg(REG_STATUS_REG_B);
   use_24h = !!(reg_b & (1 << 1));
   use_binary = !!(reg_b & (1 << 2));

   while (cmos_is_update_in_progress()); // wait an eventual update to complete
   cmod_read_datetime_raw(&d);

   do {

      dlast = d;
      while (cmos_is_update_in_progress());//wait an eventual update to complete
      cmod_read_datetime_raw(&d);

      /*
       * Read until we get the same result twice: this is necessary to get a
       * consistent set of values.
       */

   } while (dlast.raw != d.raw);

   hour_pm_bit = d.hour & 0x80;
   d.hour &= ~0x80;

   if (!use_binary) {
      d.sec = bcd_to_dec(d.sec);
      d.min = bcd_to_dec(d.min);
      d.hour = bcd_to_dec(d.hour);
      d.day = bcd_to_dec(d.day);
      d.month = bcd_to_dec(d.month);
      d.year = bcd_to_dec((u8) d.year);
   }

   if (!use_24h) {
      if (d.hour == 12) {
         if (!hour_pm_bit)
            d.hour = 0; /* 12 am is midnight => hour 0 */
      } else {
         if (hour_pm_bit)
            d.hour = (d.hour + 12) % 24;
      }
   }

   /*
    * This allows to support years from 1970 to 2069,
    * without knowing the century. Yes, knowing the century is a mess and
    * requires asking through ACPI (if supported) for the "century" register.
    * See: https://wiki.osdev.org/CMOS.
    */

   d.year = (u16)(d.year + (d.year < 70 ? 2000 : 1900));
   *out = d;
}

/*
 * Update-Ended Interrupt (UIE) support.
 * =====================================
 *
 * The CMOS RTC will assert IRQ 8 each time it completes an internal
 * update of the time registers -- once per wall-clock second -- if
 * the UIE bit (Register B, bit 4) is set. The interrupt-flag bit
 * (UF in Register C, bit 4) latches; reading Register C clears all
 * of the chip's interrupt-flag bits (UF/AF/PF) atomically. Without
 * reading Register C, the chip will not raise IRQ 8 again, so the
 * IRQ handler MUST read it even if we don't otherwise care about
 * AF/PF.
 *
 * The handler signals a kcond after snapshotting __time_ns at the
 * moment the IRQ fired -- that snapshot is the precise anchor
 * point a caller uses to align system time against the RTC. UIE is
 * enabled on demand (rtc_wait_for_second_edge), waited on, then
 * disabled: we don't keep IRQ 8 firing unless someone's about to
 * consume the signal.
 */

extern u64 __time_ns;                 /* defined in kernel/timer.c */

static struct kcond rtc_uie_cond;
static volatile u64 rtc_uie_signal_time_ns;
static u64 rtc_uie_last_accepted_edge_ns;

/*
 * Minimum acceptable gap, in ns of __time_ns, between two consecutive
 * edges returned by rtc_wait_for_second_edge(). On real hardware the
 * RTC chip ticks at exactly 1 Hz, so consecutive edges are ~1 s
 * apart. In a VM (QEMU/KVM), the chip's UIE schedule can shift
 * forward by a fraction of a second when the host preempts the guest
 * -- the chip re-anchors its 1-Hz cadence to the new wall-clock
 * boundary, but __time_ns (driven by the PIT) has already caught up,
 * so the next UF event lands at a sub-second offset in __time_ns
 * terms. Reject those events and keep waiting; the next "real" edge
 * lands ~1 s later on the new schedule.
 *
 * 500 ms is the gap: comfortably above any phase shift we've seen,
 * comfortably below the ~1 s real cadence.
 */
#define RTC_UIE_MIN_EDGE_INTERVAL_NS  (500ull * 1000ull * 1000ull)

/*
 * Cap on filtered kcond_wait passes per call. The caller's timeout
 * already bounds total wait time; this is the structural cap so the
 * loop is not unconditional. 8 is well above any rate we've ever
 * observed; if the chip really were firing UF that fast the call
 * gives up and the caller (drift kthread or selftest) sees a
 * timeout, which is the correct signal that the hardware misbehaves.
 */
#define RTC_UIE_MAX_FILTER_ATTEMPTS   8

static void rtc_enable_uie(void)
{
   u8 reg_b = (u8) cmos_read_reg(REG_STATUS_REG_B);
   cmos_write_reg(REG_STATUS_REG_B, reg_b | REG_B_UIE);
   /*
    * Clear any stale interrupt flag from before UIE was enabled
    * (or from a previous enable cycle). Without this, the very
    * first kcond_wait might return immediately on a leftover flag
    * that doesn't correspond to a real edge.
    */
   (void) cmos_read_reg(REG_STATUS_REG_C);
}

static void rtc_disable_uie(void)
{
   u8 reg_b = (u8) cmos_read_reg(REG_STATUS_REG_B);
   cmos_write_reg(REG_STATUS_REG_B, reg_b & ~REG_B_UIE);
}

static void rtc_uie_signal_waiters(void *unused)
{
   /*
    * Runs in worker-thread context (bottom half of the UIE IRQ).
    * kcond_signal_*() can't run in IRQ context -- it walks the
    * wait_list and can transition tasks RUNNABLE, which the IRQ
    * paths assert against. Doing it here adds the worker-dispatch
    * latency (sub-ms in practice) to the second-edge precision,
    * which is fine: the precision the caller cares about is the
    * `rtc_uie_signal_time_ns` snapshot, taken inside the IRQ
    * handler at the actual instant of the edge.
    */
   kcond_signal_all(&rtc_uie_cond);
}

static enum irq_action rtc_uie_irq_handler(void *ctx)
{
   const u8 reg_c = (u8) cmos_read_reg(REG_STATUS_REG_C);

   /*
    * Reading Register C clears UF/AF/PF unconditionally. We still
    * need to filter: a shared IRQ 8 (alarm or periodic enabled
    * by something else) could fire without UF set. Return
    * IRQ_NOT_HANDLED in that case so a chained handler can claim
    * the IRQ; we never share IRQ 8 today but the chain in
    * arch_irq_handling iterates so being correct here costs
    * nothing.
    */
   if (!(reg_c & REG_C_UF))
      return IRQ_NOT_HANDLED;

   /*
    * Snapshot __time_ns at the moment the second ticked. We're in
    * IRQ context with interrupts disabled (arch_irq_handling
    * re-enables them only between push/pop), so the timer IRQ's
    * own update of __time_ns can't be racing this read. u64 on
    * i386 isn't atomic, but the only other writer is the timer
    * IRQ which can't preempt us.
    */
   rtc_uie_signal_time_ns = __time_ns;

   /*
    * Defer the kcond_signal_all() to a worker. If the enqueue
    * fails (queue full -- very unlikely at 1 IRQ/sec), the waiter
    * times out and the caller retries; we don't panic for what
    * would only be a temporary fairness glitch in the drift
    * compensation loop. Just log it.
    */
   if (!wth_enqueue_anywhere(WTH_PRIO_HIGHEST,
                             &rtc_uie_signal_waiters,
                             NULL))
   {
      printk("rtc: ERROR: UIE worker enqueue failed\n");
   }

   return IRQ_HANDLED;
}

DEFINE_IRQ_HANDLER_NODE(rtc_uie_handler, rtc_uie_irq_handler, NULL);

void init_rtc_uie(void)
{
   /*
    * Set up the kcond and install the IRQ 8 handler unconditionally
    * at boot. Both operations are sub-microsecond (a kcond_init is
    * a struct zero-fill; irq_install_handler is a list_add + an
    * IMR write); doing them lazily on first use would just add a
    * branch to every wait without saving anything meaningful. UIE
    * itself stays OFF on the chip until a caller explicitly waits.
    */
   kcond_init(&rtc_uie_cond);
   irq_install_handler(X86_PC_RTC_IRQ, &rtc_uie_handler);
}

bool rtc_wait_for_second_edge(u64 *time_ns_out, u32 timeout_ticks)
{
   const u64 deadline_ticks = get_ticks() + timeout_ticks;
   u64 edge_ns = 0;
   bool got_valid_edge = false;

   rtc_enable_uie();

   /*
    * Filter loop: accept the first edge whose __time_ns snapshot is
    * at least RTC_UIE_MIN_EDGE_INTERVAL_NS past the previously
    * accepted one (see the macro's comment for the why). Both the
    * caller's deadline and a hard attempt cap bound the wait: time
    * for the normal case, attempts as a safety net against hardware
    * that keeps firing sub-interval events forever.
    */
   for (int attempt = 0; attempt < RTC_UIE_MAX_FILTER_ATTEMPTS; attempt++) {

      const u64 now_ticks = get_ticks();
      u32 remaining;
      u64 since_last;

      if (now_ticks >= deadline_ticks)
         break;

      remaining = (u32)(deadline_ticks - now_ticks);
      if (!kcond_wait(&rtc_uie_cond, NULL, remaining))
         break;     /* kcond timed out */

      edge_ns = rtc_uie_signal_time_ns;
      since_last = edge_ns - rtc_uie_last_accepted_edge_ns;

      if (since_last >= RTC_UIE_MIN_EDGE_INTERVAL_NS) {
         got_valid_edge = true;
         break;
      }
   }

   rtc_disable_uie();

   if (got_valid_edge) {
      rtc_uie_last_accepted_edge_ns = edge_ns;
      if (time_ns_out)
         *time_ns_out = edge_ns;
   }

   return got_valid_edge;
}
