/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>

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
#include <tilck/kernel/kb.h>

static struct list se_list = make_list(se_list);
static volatile bool se_stop_requested;
static struct self_test *se_running;

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
   char shortname[SELF_TEST_MAX_NAME_LEN+1];
   size_t len = strlen(name);
   const char *p = name + len - 1;
   const char *p2 = name;
   char *s = shortname;
   struct self_test *pos;

   if (len >= SELF_TEST_MAX_NAME_LEN)
      return NULL;

   /*
    * Find the position of the last '_', going backwards.
    * Reason: drop the {_manual, _short, _med, _long} suffix.
    */
   while (p > name) {

      if (*p == '_')
         break;

      p--;
   }

   if (p <= name)
      return NULL;

   while (p2 < p)
      *s++ = *p2++;

   *s = 0;

   list_for_each_ro(pos, &se_list, node) {
      if (!strcmp(pos->name, shortname))
         return pos;
   }

   return NULL;
}

void se_internal_run(struct self_test *se)
{
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

static enum kb_handler_action
se_keypress_handler(struct kb_dev *kb, struct key_event ke)
{
   enum kb_handler_action ret = kb_handler_nak;

   if (!se_running)
      return ret;

   disable_preemption();

   if (se_running) {
      if (ke.print_char == 'c' && kb_is_ctrl_pressed(kb)) {
         se_stop_requested = true;
         ret = kb_handler_ok_and_stop;
      }
   }

   enable_preemption();
   return ret;
}

void regular_self_test_end(void)
{
   printk("Self-test completed.\n");
}

static struct keypress_handler_elem se_handler =
{
   .handler = &se_keypress_handler
};

void init_self_tests(void)
{
   struct self_test *se;
   list_for_each_ro(se, &se_list, node) {
      se_actual_register(se);
   }

   register_keypress_handler(&se_handler);
}

void selftest_list_manual(void)
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

DECLARE_AND_REGISTER_SELF_TEST(list, se_manual, &selftest_list_manual)
