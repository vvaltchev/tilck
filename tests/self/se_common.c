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
   char name_buf[SELF_TEST_MAX_NAME_LEN+1];
   const char *name_to_use = name;
   size_t len = strlen(name);
   const char *p = name + len - 1;
   const char *p2 = name;
   char *s = name_buf;
   struct self_test *pos;

   if (len >= SELF_TEST_MAX_NAME_LEN)
      return NULL;

   /*
    * Find the position of the last '_', going backwards.
    * Reason: drop the {_manual, _short, _med, _long} suffix.
    */
   while (p > name) {

      if (*p == '_') {

         if (strcmp(p, "_manual") &&
             strcmp(p, "_short")  &&
             strcmp(p, "_med")    &&
             strcmp(p, "_long"))
         {
            /*
             * Some self-tests like kmutex_ord use '_' in their name. In those
             * cases, we should never discard whatever was after the last '_'.
             */
            p = name;
         }

         break;
      }

      p--;
   }

   if (p > name) {

      while (p2 < p)
         *s++ = *p2++;

      *s = 0;
      name_to_use = name_buf;
   }

   list_for_each_ro(pos, &se_list, node) {
      if (!strcmp(pos->name, name_to_use))
         return pos;
   }

   return NULL;
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
