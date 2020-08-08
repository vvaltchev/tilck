/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/cmdline.h>
#include <tilck/kernel/self_tests.h>

static int no_deadlock_set_elems;
static int no_deadlock_set[MAX_NO_DEADLOCK_SET_ELEMS];
static u64 no_deadlock_set_progress[MAX_NO_DEADLOCK_SET_ELEMS];
static u64 no_deadlock_set_progress_old[MAX_NO_DEADLOCK_SET_ELEMS];

void debug_reset_no_deadlock_set(void)
{
   disable_preemption();
   {
      no_deadlock_set_elems = 0;
      bzero(no_deadlock_set, sizeof(no_deadlock_set));
      bzero(no_deadlock_set_progress, sizeof(no_deadlock_set_progress));
      bzero(no_deadlock_set_progress_old, sizeof(no_deadlock_set_progress_old));
   }
   enable_preemption();
}

void debug_add_task_to_no_deadlock_set(int tid)
{
   disable_preemption();
   {
      for (int i = 0; i < no_deadlock_set_elems; i++) {
         VERIFY(no_deadlock_set[i] != tid);
      }

      VERIFY(no_deadlock_set_elems < (int)ARRAY_SIZE(no_deadlock_set));
      no_deadlock_set[no_deadlock_set_elems++] = tid;
   }
   enable_preemption();
}

static int nds_find_pos(int tid)
{
   ASSERT(!is_preemption_enabled());

   for (int i = 0; i < no_deadlock_set_elems; i++) {
      if (no_deadlock_set[i] == tid)
         return i;
   }

   return -1;
}

void debug_remove_task_from_no_deadlock_set(int tid)
{
   int pos;

   disable_preemption();
   {
      pos = nds_find_pos(tid);

      if (pos < 0)
         panic("Task %d not found in no_deadlock_set", tid);

      no_deadlock_set[pos] = 0;
   }
   enable_preemption();
}

void debug_no_deadlock_set_report_progress(void)
{
   int tid = get_curr_tid();
   int pos;

   disable_preemption();
   {
      pos = nds_find_pos(tid);
      VERIFY(pos >= 0);
      no_deadlock_set_progress[pos]++;
   }
   enable_preemption();
}

void debug_check_for_deadlock(void)
{
   bool found_runnable = false;
   struct task *ti;
   int tid, candidates = 0;

   disable_preemption();
   {
      for (int i = 0; i < no_deadlock_set_elems; i++) {

         if (!(tid = no_deadlock_set[i]))
            continue;

         if (!(ti = get_task(tid)))
            continue;

         candidates++;

         if (ti->state == TASK_STATE_RUNNABLE) {
            found_runnable = true;
            break;
         }
      }
   }
   enable_preemption();

   if (candidates > 0 && !found_runnable) {
      panic("No runnable task found in no_deadlock_set [%d elems]", candidates);
   }
}

static bool
nds_should_skip_progress_check(int i, int *tid_ref)
{
   int tid;
   struct task *ti;
   ASSERT(!is_preemption_enabled());

   if (!no_deadlock_set_progress[i])
      return true;

   if (!(tid = no_deadlock_set[i]))
      return true;

   if (!(ti = get_task(tid)))
      return true;

   if (ti->state == TASK_STATE_ZOMBIE)
      return true;

   if (tid_ref)
      *tid_ref = tid;

   return false;
}

static void
nds_dump_progress_of_first_tasks(void)
{
   int counter = 0;
   ASSERT(!is_preemption_enabled());

   printk("Progress of the first tasks: [\n");

   for (int i = 0; i < no_deadlock_set_elems && counter < 4; i++) {

      if (nds_should_skip_progress_check(i, NULL))
         continue;

      if (i == 0)
         printk("%" PRIu64 " ", no_deadlock_set_progress[i]);
      else
         printk(NO_PREFIX "%" PRIu64 " ", no_deadlock_set_progress[i]);

      counter++;
   }

   printk(NO_PREFIX "\n");
   printk("]\n\n");
}

void debug_check_for_any_progress(void)
{
   int tid;
   int candidates = 0;

   disable_preemption();
   {
      for (int i = 0; i < no_deadlock_set_elems; i++) {

         if (nds_should_skip_progress_check(i, &tid))
            continue;

         if (no_deadlock_set_progress[i] == no_deadlock_set_progress_old[i])
            panic("[deadlock?] No progress for tid %d", tid);

         candidates++;
      }

      if (candidates)
         nds_dump_progress_of_first_tasks();

      memcpy(no_deadlock_set_progress_old,
             no_deadlock_set_progress,
             sizeof(no_deadlock_set_progress));
   }
   enable_preemption();
}
