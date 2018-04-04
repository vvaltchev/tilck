
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/list.h>
#include <exos/kmalloc.h>
#include <exos/process.h>
#include <exos/hal.h>

#define TIME_SLOT_JIFFIES (TIMER_HZ / 50) /* 20 ms */

task_info *current;

// Our linked list for all the tasks (processes, threads, etc.)
list_node tasks_list = make_list_node(tasks_list);
list_node runnable_tasks_list = make_list_node(runnable_tasks_list);
list_node sleeping_tasks_list = make_list_node(sleeping_tasks_list);

static int runnable_tasks_count;
static int current_max_pid;
static task_info *idle_task;
static u64 idle_ticks;

int create_new_pid(void)
{
   int r = -1;
   ASSERT(!is_preemption_enabled());

   for (int i = 1; i <= MAX_PID; i++) {

      int pid = (current_max_pid + i) % MAX_PID;

      if (!get_task(pid)) {
         current_max_pid = pid;
         r = pid;
         break;
      }
   }

   return r;
}

void idle_task_kthread(void)
{
   while (true) {

      idle_ticks++;
      halt();

      if (runnable_tasks_count > 0)
         kernel_yield();
   }
}

void initialize_scheduler(void)
{
   idle_task = kthread_create(&idle_task_kthread, NULL);
}

void set_current_task_in_kernel(void)
{
   ASSERT(!is_preemption_enabled());
   current->running_in_kernel = true;
}

void task_add_to_state_list(task_info *ti)
{
   switch (ti->state) {

   case TASK_STATE_RUNNABLE:
      list_add_tail(&runnable_tasks_list, &ti->runnable_list);
      runnable_tasks_count++;
      break;

   case TASK_STATE_SLEEPING:
      list_add_tail(&sleeping_tasks_list, &ti->sleeping_list);
      break;

   case TASK_STATE_RUNNING:
      /* no dedicated list: without SMP there's only one 'running' task */
      break;

   case TASK_STATE_ZOMBIE:
      /* no dedicated list for 'zombie' tasks at the moment */
      break;

   default:
      NOT_REACHED();
   }
}

void task_remove_from_state_list(task_info *ti)
{
   switch (ti->state) {

   case TASK_STATE_RUNNABLE:
      list_remove(&ti->runnable_list);
      runnable_tasks_count--;
      ASSERT(runnable_tasks_count >= 0);
      break;

   case TASK_STATE_SLEEPING:
      list_remove(&ti->sleeping_list);
      break;

   case TASK_STATE_RUNNING:
   case TASK_STATE_ZOMBIE:
      break;

   default:
      NOT_REACHED();
   }
}

void task_change_state(task_info *ti, task_state_enum new_state)
{
   disable_preemption();
   {
      ASSERT(ti->state != new_state);
      task_remove_from_state_list(ti);

      ti->state = new_state;

      task_add_to_state_list(ti);
   }
   enable_preemption();
}

void add_task(task_info *ti)
{
   disable_preemption();
   {
      list_add_tail(&tasks_list, &ti->list);
      task_add_to_state_list(ti);
   }
   enable_preemption();
}

void remove_task(task_info *ti)
{
   disable_preemption();
   {
      ASSERT(ti->state == TASK_STATE_ZOMBIE);

      task_remove_from_state_list(ti);
      list_remove(&ti->list);

      kfree2(ti->kernel_stack, KTHREAD_STACK_SIZE);
      kfree2(ti, sizeof(task_info));
   }
   enable_preemption();
}

void account_ticks(void)
{
   if (!current) {
      return;
   }

   current->ticks++;
   current->total_ticks++;

   if (current->running_in_kernel) {
      current->kernel_ticks++;
   }
}

bool need_reschedule(void)
{
   if (!current) {
      // The kernel is still initializing and we cannot call schedule() yet.
      return false;
   }

   if (current->ticks < TIME_SLOT_JIFFIES &&
       current->state == TASK_STATE_RUNNING) {
      return false;
   }

   return true;
}

void schedule_outside_interrupt_context(void)
{
   // HACK: push a fake interrupt to compensate the call to
   // pop_nested_interrupt() in switch_to_task(task_info *).

   push_nested_interrupt(-1);

   schedule();

   /*
    * If we're here it's because schedule() just returned: this happens
    * when the only runnable task is the current one.
    */
   pop_nested_interrupt();
}

NORETURN void switch_to_idle_task(void)
{
   switch_to_task(idle_task);
}

NORETURN void switch_to_idle_task_outside_interrupt_context(void)
{
   // HACK: push a fake interrupt to compensate the call to
   // pop_nested_interrupt() in switch_to_task(task_info *).

   push_nested_interrupt(-1);
   switch_to_task(idle_task);
}


void schedule(void)
{
   task_info *selected = NULL;
   task_info *pos;
   u64 least_ticks_for_task = (u64)-1;

   ASSERT(!is_preemption_enabled());

   // If we preempted the process, it is still runnable.
   if (current->state == TASK_STATE_RUNNING) {
      task_change_state(current, TASK_STATE_RUNNABLE);
   }

   list_for_each(pos, &runnable_tasks_list, runnable_list) {

      ASSERT(pos->state == TASK_STATE_RUNNABLE);

      if (pos == idle_task)
         continue;

      if (pos->total_ticks < least_ticks_for_task) {
         selected = pos;
         least_ticks_for_task = pos->total_ticks;
      }
   }

   if (!selected) {
      selected = idle_task;
   }

   if (selected == current) {
      task_change_state(selected, TASK_STATE_RUNNING);
      selected->ticks = 0;
      return;
   }

   switch_to_task(selected);
}

// TODO: make this function much faster (e.g. indexing by tid)
task_info *get_task(int tid)
{
   task_info *pos;
   task_info *res = NULL;

   disable_preemption();

   list_for_each(pos, &tasks_list, list) {
      if (pos->tid == tid) {
         res = pos;
         break;
      }
   }

   enable_preemption();
   return res;
}
