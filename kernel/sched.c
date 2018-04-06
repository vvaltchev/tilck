
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/list.h>
#include <exos/kmalloc.h>
#include <exos/process.h>
#include <exos/hal.h>


#define TIME_SLOT_JIFFIES (TIMER_HZ / 50) /* 20 ms */

task_info *current;
task_info *kernel_process;

list_node runnable_tasks_list;
list_node sleeping_tasks_list;
static task_info *tree_by_tid_root;

static u64 idle_ticks;
static int runnable_tasks_count;
static int current_max_pid = -1;
static task_info *idle_task;


static int ti_insert_remove_cmp(const void *a, const void *b)
{
   const task_info *t1 = a;
   const task_info *t2 = b;
   return t1->tid - t2->tid;
}

static int ti_find_cmp(const void *obj, const void *valptr)
{
   const task_info *task = obj;
   int searched_tid = *(const int *)valptr;
   return task->tid - searched_tid;
}

typedef struct {

   int lowest_available;
   int lowest_after_current_max;

} create_pid_visit_ctx;

static int create_new_pid_visit_cb(void *obj, void *arg)
{
   task_info *ti = obj;
   create_pid_visit_ctx *ctx = arg;

   if (ti->tid != ti->owning_process_pid)
      return 0; /* skip threads */

   /*
    * Algorithm: we start with lowest_available (L) == 0. When we hit
    * tid == L, that means L is not really the lowest, therefore, we guess
    * the right value of L is L + 1. The first time tid skips one, for example
    * jumping from 3 to 5, the value of L set by the iteration with tid == 3,
    * will stuck. That value will be clearly 4.
    */

   if (ctx->lowest_available == ti->tid)
      ctx->lowest_available = ti->tid + 1;

   /*
    * For lowest_after_current_max (A) the logic is similar.
    * We start with A = current_max_pid + 1. The first time A is == tid, will
    * be when tid is current_max_pid + 1. We continue to update A, until the
    * first whole is found. In case tid never reaches current_max_pid + 1,
    * A will be just be current_max_pid + 1, as expected.
    */

   if (ctx->lowest_after_current_max == ti->tid)
      ctx->lowest_after_current_max = ti->tid + 1;

   return 0;
}

int create_new_pid(void)
{
   ASSERT(!is_preemption_enabled());
   create_pid_visit_ctx ctx = { 0, current_max_pid + 1 };
   int r;

   bintree_in_order_visit(tree_by_tid_root,
                          create_new_pid_visit_cb,
                          &ctx,
                          task_info,
                          tree_by_tid);

   r = ctx.lowest_after_current_max <= MAX_PID
         ? ctx.lowest_after_current_max
         : (ctx.lowest_available <= MAX_PID ? ctx.lowest_available : -1);

   if (r >= 0)
      current_max_pid = r;

   //printk("[kernel] create_new_pid: %i\n", r);
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
   list_node_init(&runnable_tasks_list);
   list_node_init(&sleeping_tasks_list);

   int kernel_pid = create_new_pid();
   ASSERT(kernel_pid == 0);

   kernel_process = allocate_new_process(NULL, kernel_pid);
   VERIFY(kernel_process != NULL); // This failure CANNOT be handled.
   ASSERT(kernel_process->pi->parent_pid == 0);

   kernel_process->running_in_kernel = true;
   kernel_process->pi->pdir = get_kernel_page_dir();
   memcpy(kernel_process->pi->cwd, "/", 2);

   kernel_process->state = TASK_STATE_SLEEPING;
   add_task(kernel_process);

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
      task_add_to_state_list(ti);

      bintree_insert(&tree_by_tid_root,
                     ti,
                     ti_insert_remove_cmp,
                     task_info,
                     tree_by_tid);
   }
   enable_preemption();
}

void remove_task(task_info *ti)
{
   disable_preemption();
   {
      ASSERT(ti->state == TASK_STATE_ZOMBIE);

      task_remove_from_state_list(ti);

      bintree_remove(&tree_by_tid_root,
                     ti,
                     ti_insert_remove_cmp,
                     task_info,
                     tree_by_tid);

      free_task(ti);
   }
   enable_preemption();
}

void account_ticks(void)
{
   if (!current)
      return;

   current->time_slot_ticks++;
   current->total_ticks++;

   if (current->running_in_kernel)
      current->total_kernel_ticks++;
}

bool need_reschedule(void)
{
   if (!current) {
      // The kernel is still initializing and we cannot call schedule() yet.
      return false;
   }

   if (current->time_slot_ticks < TIME_SLOT_JIFFIES &&
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
      selected->time_slot_ticks = 0;
      return;
   }

   switch_to_task(selected);
}


task_info *get_task(int tid)
{
   task_info *res = NULL;

   disable_preemption();
   {
      res = bintree_find(tree_by_tid_root,
                         &tid,
                         ti_find_cmp,
                         task_info,
                         tree_by_tid);
   }

   enable_preemption();
   return res;
}
