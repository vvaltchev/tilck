/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/datetime.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/self_tests.h>
#include <tilck/kernel/sched.h>

extern u32 __tick_duration;

void selftest_time_manual(void)
{
   const int art_drift_p = 5;
   struct datetime d;
   s64 sys_ts, hw_ts;
   int id = 0, drift;
   u32 orig_tick_duration;
   uptr var;

    /*
     * Increase tick's actual duration by 5% in order to produce quickly a
     * huge clock drift. Note: consider that __tick_duration is added to the
     * current time, TIMER_HZ times per second.
     *
     * For example, with TIMER_HZ=100:
     *
     *   td == 0.01 [ideal tick duration]
     *
     * Increasing `td` by 5%:
     *
     *   td == 0.0105
     *
     * Now after 1 second, we have an artificial drift of:
     *   0.0005 s * 100 = 0.05 s.
     *
     * After 20 seconds, we'll have a drift of 1 second.
     *
     * NOTE:
     *
     * A positive drift (calculated as: sys_ts - hw_ts) means that we're
     * going too fast and we have to add a _negative_ adjustment.
     *
     * A negative drift, means that we're lagging behind and we need to add a
     * _positive_ adjustment.
     */
   disable_interrupts(&var);
   {
      orig_tick_duration = __tick_duration;
      __tick_duration = __tick_duration * (100+art_drift_p) / 100;
   }
   enable_interrupts(&var);

   printk("Clock drift correction self-test\n");
   printk("---------------------------------------------\n\n");

   for (int t = 0; t < 4 * 60; t++) {

      if (se_is_stop_requested())
         break;

      hw_read_clock(&d);
      hw_ts = datetime_to_timestamp(d);
      sys_ts = get_timestamp();
      drift = -id + (int)(sys_ts - hw_ts);

      if (t == 0) {

         /* Save the initial drift */
         id = drift;
         printk("NOTE: Introduce artificial drift of %d%%\n", art_drift_p);

      } else if (t == 60 || t == 180) {

         printk("NOTE: Remove any artificial drift\n");
         disable_interrupts(&var);
         {
            __tick_duration = orig_tick_duration;
         }
         enable_interrupts(&var);

      } else if (t == 120) {

         printk("NOTE: Introduce artificial drift of -%d%%\n", art_drift_p);
         disable_interrupts(&var);
         {
            __tick_duration = __tick_duration * (100-art_drift_p) / 100;
         }
         enable_interrupts(&var);
      }

      printk(NO_PREFIX "[%06d seconds] Drift: %d\n", t, t ? drift : 0);
      kernel_sleep(TIMER_HZ);
   }

   disable_interrupts(&var);
   {
      __tick_duration = orig_tick_duration;
   }
   enable_interrupts(&var);
   regular_self_test_end();
}
