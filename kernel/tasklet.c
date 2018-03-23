
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/tasklet.h>
#include <exos/kmalloc.h>
#include <exos/hal.h>
#include <exos/sync.h>
#include <exos/process.h>

typedef void (*tasklet_func)(uptr, uptr, uptr);

typedef struct {

   tasklet_func fptr;
   tasklet_context ctx;

} tasklet;

tasklet *all_tasklets;

static task_info *tasklet_runner_task;
static int first_free_slot_index;
static int slots_used;
static int tasklet_to_execute;
static kcond tasklet_cond;

void initialize_tasklets()
{
   all_tasklets = kmalloc(sizeof(tasklet) * MAX_TASKLETS);

   ASSERT(all_tasklets != NULL);
   bzero(all_tasklets, sizeof(tasklet) * MAX_TASKLETS);

   kcond_init(&tasklet_cond);
   tasklet_runner_task = kthread_create(tasklet_runner_kthread, NULL);
}


bool enqueue_tasklet_int(void *func, uptr arg1, uptr arg2, uptr arg3)
{
   disable_interrupts();
   ASSERT(all_tasklets != NULL);

   if (slots_used >= MAX_TASKLETS) {
      enable_interrupts();
      return false;
   }

   ASSERT(all_tasklets[first_free_slot_index].fptr == NULL);

   all_tasklets[first_free_slot_index].fptr = (tasklet_func)func;
   all_tasklets[first_free_slot_index].ctx.arg1 = arg1;
   all_tasklets[first_free_slot_index].ctx.arg2 = arg2;
   all_tasklets[first_free_slot_index].ctx.arg3 = arg3;

   first_free_slot_index = (first_free_slot_index + 1) % MAX_TASKLETS;
   slots_used++;

   enable_interrupts();

   /*
    * Special way of signalling a condition variable, without holding its lock:
    * this code will be often often called by higher-halfs of interrupt handlers
    * so it won't be possible to acquire a lock there. This means that the wait
    * on the other side may miss a signal (not waiting while we fire the signal
    * here) but that's OK since the tasklet runner thread calls run_one_tasklet
    * in a while(true) loop.
    */
   kcond_signal_single(&tasklet_cond, tasklet_runner_task);

   return true;
}

bool run_one_tasklet(void)
{
   tasklet t;
   disable_interrupts();
   ASSERT(all_tasklets != NULL);

   if (slots_used == 0) {
      enable_interrupts();
      return false;
   }

   ASSERT(all_tasklets[tasklet_to_execute].fptr != NULL);

   memmove(&t, &all_tasklets[tasklet_to_execute], sizeof(tasklet));
   all_tasklets[tasklet_to_execute].fptr = NULL;

   slots_used--;
   tasklet_to_execute = (tasklet_to_execute + 1) % MAX_TASKLETS;

   enable_interrupts();

   /* Execute the tasklet with preemption ENABLED */
   t.fptr(t.ctx.arg1, t.ctx.arg2, t.ctx.arg3);

   return true;
}


void tasklet_runner_kthread()
{
   printk("[kernel thread] tasklet runner kthread (pid: %i)\n",
          get_current_task()->pid);

   while (true) {

      bool tasklet_run;

      do {

         tasklet_run = run_one_tasklet();

      } while (tasklet_run);

      /*
       * Special use of a condition variable without a mutex, see the comment
       * above in enqueue_tasklet_int().
       */
      kcond_wait(&tasklet_cond, NULL, TIMER_HZ / 10);
   }
}
