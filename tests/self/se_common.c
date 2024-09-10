/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/irq.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/debug_utils.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/worker_thread.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/fault_resumable.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/self_tests.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/errno.h>

static struct list se_list = STATIC_LIST_INIT(se_list);
static volatile bool se_stop_requested;
static struct self_test *se_running;
static struct task *se_user_task;

static void
se_actual_register(struct self_test *se)
{
   struct self_test *pos;

   if (strlen(se->name) >= SELF_TEST_MAX_NAME_LEN)
      panic("Self test name '%s' too long\n", se->name);

   list_for_each_ro(pos, &se_list, node) {

      if (pos == se)
         continue;

      if (!strcmp(pos->name, se->name))
         panic("Cannot register self-test '%s': duplicate name!", se->name);
   }
}

void se_register(struct self_test *se)
{
   list_add_tail(&se_list, &se->node);
}

bool se_is_stop_requested(void)
{
   return se_stop_requested;
}

struct self_test *se_find(const char *name)
{
   struct self_test *pos;

   list_for_each_ro(pos, &se_list, node) {
      if (!strcmp(pos->name, name))
         return pos;
   }

   return NULL;
}

int se_runall(void)
{
   struct self_test *pos;
   int rc = 0;

   list_for_each_ro(pos, &se_list, node) {

      if (pos->kind != se_manual) {
         printk("\033[93m[selftest]\033[0m [RUN   ] %s\n", pos->name);

         if ((rc = se_run(pos))) {
            printk("\033[93m[selftest]\033[0m [FAILED] %s\n", pos->name);
            break;
         }
         printk("\033[93m[selftest]\033[0m [PASSED] %s\n", pos->name);
      }
   }

   return rc;
}

static void se_internal_run(struct self_test *se)
{
   ASSERT(se_user_task != NULL);

   /* Common self test setup code */
   disable_preemption();
   {
      se_stop_requested = false;
      se_running = se;
   }
   enable_preemption();

   /* Run the actual self test */
   se->func();

   /* Common self test tear down code */
   disable_preemption();
   {
      se_stop_requested = false;
      se_running = NULL;
   }
   enable_preemption();
}

int se_run(struct self_test *se)
{
   int tid;
   int rc = 0;

   disable_preemption();
   {
      if (se_running) {

         printk("self-tests: parallel runs not allowed (tid: %d)\n",
                get_curr_tid());

         enable_preemption();
         return -EBUSY;
      }

      se_user_task = get_curr_task();
   }
   enable_preemption();

   tid = kthread_create(se_internal_run, KTH_ALLOC_BUFS, se);

   if (tid > 0) {

      rc = kthread_join(tid, false);

      if (rc) {
         se_stop_requested = true;
         printk("self-tests: stop requested\n");
         rc = kthread_join(tid, true);
      }

   } else {

      printk("self-tests: kthread_create() failed with: %d\n", tid);
      rc = tid;
   }

   disable_preemption();
   {
      se_user_task = NULL;
   }
   enable_preemption();
   return rc;
}

void se_regular_end(void)
{
   printk("Self-test completed.\n");
}

void se_interrupted_end(void)
{
   printk("Self-test interrupted.\n");
}

void init_self_tests(void)
{
   struct self_test *se;
   list_for_each_ro(se, &se_list, node) {
      se_actual_register(se);
   }
}

void selftest_list(void)
{
   static const char *se_kind_str[] = {
      [se_short] = "short",
      [se_med] = "med",
      [se_long] = "long",
      [se_manual] = "manual"
   };

   struct self_test *se;
   list_for_each_ro(se, &se_list, node) {
      printk("%-20s [%s]\n", se->name, se_kind_str[se->kind]);
   }
}

REGISTER_SELF_TEST(list, se_manual, &selftest_list)
