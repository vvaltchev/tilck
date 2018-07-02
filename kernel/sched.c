
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/list.h>
#include <exos/kmalloc.h>
#include <exos/process.h>
#include <exos/hal.h>
#include <exos/tasklet.h>
#include <exos/timer.h>

task_info *__current;
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

   //printk("create_new_pid: %i\n", r);
   return r;
}

void idle_task_kthread(void)
{
   while (true) {

      ASSERT(is_preemption_enabled());

      idle_ticks++;
      halt();

      if (runnable_tasks_count > 0)
         kernel_yield();
   }
}

void create_kernel_process(void)
{
   static task_info s_kernel_ti;
   static process_info s_kernel_pi;

   list_node_init(&runnable_tasks_list);
   list_node_init(&sleeping_tasks_list);

   int kernel_pid = create_new_pid();
   ASSERT(kernel_pid == 0);

   s_kernel_pi.ref_count = 1;
   s_kernel_ti.tid = kernel_pid;
   s_kernel_ti.owning_process_pid = kernel_pid;

   s_kernel_ti.pi = &s_kernel_pi;
   bintree_node_init(&s_kernel_ti.tree_by_tid);
   list_node_init(&s_kernel_ti.runnable_list);
   list_node_init(&s_kernel_ti.sleeping_list);

   arch_specific_new_task_setup(&s_kernel_ti);
   ASSERT(s_kernel_pi.parent_pid == 0);

   s_kernel_ti.running_in_kernel = true;
   memcpy(s_kernel_pi.cwd, "/", 2);

   s_kernel_ti.state = TASK_STATE_SLEEPING;

   kernel_process = &s_kernel_ti;
   add_task(kernel_process);
   set_current_task(kernel_process);
}

void init_sched(void)
{
   kernel_process->pi->pdir = get_kernel_page_dir();
   idle_task = kthread_create(&idle_task_kthread, NULL);
}

void set_current_task_in_kernel(void)
{
   ASSERT(!is_preemption_enabled());
   get_curr_task()->running_in_kernel = true;
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
      ASSERT(ti->state != TASK_STATE_ZOMBIE);

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
   task_info *curr = get_curr_task();
   ASSERT(curr != NULL);

   curr->time_slot_ticks++;
   curr->total_ticks++;

   if (curr->running_in_kernel)
      curr->total_kernel_ticks++;
}

bool need_reschedule(void)
{
   task_info *curr = get_curr_task();
   ASSERT(curr != NULL);

   for (u32 tn = 0; tn < MAX_TASKLET_THREADS; tn++) {

      if (!any_tasklets_to_run(tn))
         continue;

      if (get_tasklet_runner(tn) == curr) {

         /*
          * The highest-priority tasklet runner we've found with tasklets to
          * run is the currently running task. No need to reschedule.
          * NOTE: no need to check the time_slot_ticks: a tasklet thread
          * can be preempted only by a higher priority tasklet thread and that
          * will happen anyway if such a thread exists.
          */
         return false;
      }

      /*
       * The highest-priority tasklet runner we've found with tasklets to run
       * is NOT the currently running task: we need to re-schedule.
       */
      return true;
   }

   if (curr->time_slot_ticks < TIME_SLOT_JIFFIES &&
       curr->state == TASK_STATE_RUNNING) {
      return false;
   }

   return true;
}

void schedule_outside_interrupt_context(void)
{
   schedule(-1);
}

NORETURN void switch_to_idle_task(void)
{
   switch_to_task(idle_task, X86_PC_TIMER_IRQ);
}

NORETURN void switch_to_idle_task_outside_interrupt_context(void)
{
   switch_to_task(idle_task, -1);
}

void schedule(int curr_irq)
{
   task_info *selected = NULL;
   task_info *pos;

   ASSERT(!is_preemption_enabled());

   /*
    * Tasklets (used as IRQ bottom-halfs) have absolute priority.
    * NOTE: with the current algorithm, the tasklet thread 0 has the maximum
    * priority, while the tasklet thread MAX_TASKLET_THREADS - 1 has the
    * lowest priority.
    */

   for (u32 tn = 0; tn < MAX_TASKLET_THREADS; tn++) {

      if (!any_tasklets_to_run(tn))
         continue;

      task_info *ti = get_tasklet_runner(tn);

      if (ti == get_curr_task()) {

         /*
          * The highest-priority tasklet runner is already the current task:
          * no context switch is needed.
          */
         return;
      }

      if (ti->state == TASK_STATE_RUNNABLE) {
         selected = ti;
         break;
      }
   }

   // If we preempted the process, it is still runnable.
   if (get_curr_task()->state == TASK_STATE_RUNNING) {
      task_change_state(get_curr_task(), TASK_STATE_RUNNABLE);
   }

   if (selected)
      switch_to_task(selected, curr_irq);

   list_for_each(pos, &runnable_tasks_list, runnable_list) {

      ASSERT(pos->state == TASK_STATE_RUNNABLE);

      if (pos == idle_task)
         continue;

      if (!selected || pos->total_ticks < selected->total_ticks) {
         selected = pos;
      }
   }

   if (!selected) {
      selected = idle_task;
   }

   if (selected == get_curr_task()) {
      task_change_state(selected, TASK_STATE_RUNNING);
      selected->time_slot_ticks = 0;
      return;
   }

   switch_to_task(selected, curr_irq);
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
