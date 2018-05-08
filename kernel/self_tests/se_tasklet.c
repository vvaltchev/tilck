
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <common/utils.h>

#include <exos/tasklet.h>
#include <exos/process.h>

static volatile int counter = 0;
static const int tot_iters = MAX_TASKLETS * 10;

static void test_tasklet_func()
{
   for (int i = 0; i < 10; i++) {
      counter++;
   }
}

static void end_test()
{
   VERIFY(counter == tot_iters * 10);
   printk("[selftest_tasklet] COMPLETED\n");
   debug_qemu_turn_off_machine();
}

static void selftest_tasklet(void)
{
   bool added;
   counter = 0;

   printk("[selftest_tasklet] BEGIN\n");

   for (int i = 0; i < tot_iters; i++) {

      ASSERT(is_preemption_enabled());

      do {
         disable_preemption();
         added = enqueue_tasklet0(&test_tasklet_func);
         enable_preemption();
      } while (!added);

      if (!(i % 1000)) {
         printk("[selftest_tasklet] %i%%\n", (i*100)/tot_iters);
      }
   }

   do {
      disable_preemption();
      added = enqueue_tasklet0(&end_test);
      enable_preemption();
   } while (!added);
}
