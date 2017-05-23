
#include <tasklet.h>
#include <kmalloc.h>
#include <string_util.h>
#include <hal.h>
#include <sync.h>

typedef void (*tasklet_func)(uptr, uptr, uptr);

typedef struct {

   tasklet_func fptr;
   tasklet_context ctx;

} tasklet;


tasklet *all_tasklets = NULL;
static volatile int first_free_slot_index = 0;
static volatile int slots_used = 0;
static volatile int tasklet_to_execute = 0;
static kcond tasklet_cond = {0};

void initialize_tasklets()
{
   all_tasklets = kmalloc(sizeof(tasklet) * MAX_TASKLETS);

   ASSERT(all_tasklets != NULL);
   bzero(all_tasklets, sizeof(tasklet) * MAX_TASKLETS);

   kcond_init(&tasklet_cond);
   kthread_create(tasklet_runner_kthread, NULL);
}


bool add_tasklet_int(void *func, uptr arg1, uptr arg2, uptr arg3)
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
   kcond_signal_all(&tasklet_cond);

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

      /*
       * Special use of a condition variable without a mutex, see the comment
       * above in add_tasklet_int().
       */
      kcond_wait(&tasklet_cond, NULL);

      run_one_tasklet();
   }
}
