
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <common/utils.h>

#include <exos/tasklet.h>
#include <exos/process.h>

static void test_tasklet_func()
{
   for (int i = 0; i < 10; i++) {
      asmVolatile("nop");
   }
}

static void tasklet_test_thread(void *unused)
{
   const int tot_iters = MAX_TASKLETS * 10;
   printk("[selftest_tasklet] BEGIN\n");

   for (int i = 0; i < tot_iters; i++) {

      ASSERT(is_preemption_enabled());

      bool added;

      do {
         disable_preemption();
         {
            added = enqueue_tasklet0(&test_tasklet_func);
         }
         enable_preemption();
      } while (!added);

      if (!(i % 1000)) {
         printk("[selftest_tasklet] %i%%\n", (i*100)/tot_iters);
      }
   }

   printk("[selftest_tasklet] COMPLETED\n");
   debug_qemu_turn_off_machine();
}

void selftest_tasklet(void)
{
   kthread_create(tasklet_test_thread, NULL);
}
