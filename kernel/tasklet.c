
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

void init_tasklets()
{
   all_tasklets = kmalloc(sizeof(tasklet) * MAX_TASKLETS);

   ASSERT(all_tasklets != NULL);
   bzero(all_tasklets, sizeof(tasklet) * MAX_TASKLETS);

   kcond_init(&tasklet_cond);
   tasklet_runner_task = kthread_create(tasklet_runner_kthread, NULL);
}


bool enqueue_tasklet_int(void *func, uptr arg1, uptr arg2, uptr arg3)
{
   uptr var;
   disable_interrupts(&var);
   ASSERT(all_tasklets != NULL);

   if (slots_used >= MAX_TASKLETS) {
      enable_interrupts(&var);
      return false;
   }

   ASSERT(all_tasklets[first_free_slot_index].fptr == NULL);

   all_tasklets[first_free_slot_index].fptr = (tasklet_func)func;
   all_tasklets[first_free_slot_index].ctx.arg1 = arg1;
   all_tasklets[first_free_slot_index].ctx.arg2 = arg2;
   all_tasklets[first_free_slot_index].ctx.arg3 = arg3;

   first_free_slot_index = (first_free_slot_index + 1) % MAX_TASKLETS;
   slots_used++;

   enable_interrupts(&var);

#ifndef UNIT_TEST_ENVIRONMENT

   /*
    * Special way of signalling a condition variable, without holding its lock:
    * this code will be often often called by higher-halfs of interrupt handlers
    * so it won't be possible to acquire a lock there. This means that the wait
    * on the other side may miss a signal (not waiting while we fire the signal
    * here) but that's OK since the tasklet runner thread calls run_one_tasklet
    * in a while(true) loop and it uses a timeout.
    */
   kcond_signal_single(&tasklet_cond, tasklet_runner_task);

#endif

   return true;
}

bool run_one_tasklet(void)
{
   tasklet t;
   uptr var;
   disable_interrupts(&var);
   ASSERT(all_tasklets != NULL);

   if (slots_used == 0) {
      enable_interrupts(&var);
      return false;
   }

   ASSERT(all_tasklets[tasklet_to_execute].fptr != NULL);

   memcpy(&t, &all_tasklets[tasklet_to_execute], sizeof(tasklet));
   all_tasklets[tasklet_to_execute].fptr = NULL;

   slots_used--;
   tasklet_to_execute = (tasklet_to_execute + 1) % MAX_TASKLETS;

   enable_interrupts(&var);

   /* Execute the tasklet with preemption ENABLED */
   t.fptr(t.ctx.arg1, t.ctx.arg2, t.ctx.arg3);

   return true;
}

/*
 * Debug-only checks useful to verify that kernel_yield() + context_switch()
 * do NOT change the current ESP. Sure, at some point when we decide that
 * those function will never be touched again we could remove this code, but
 * until then, in a fast-growing and changing code base like the current one,
 * it makes sense to constantly check that there are not subtle bugs in the
 * probably most critical code. The tasklet runner kernel thread seems the
 * perfect place for such checks, because it really often yields and gets the
 * control back. The DEBUG_VALIDATE_STACK_PTR() sure works as well, but it
 * catches bugs only when the stack pointer is completely out of the allocated
 * stack area for the current task. This following code allows instead, any kind
 * of such problems to be caught much earlier.
 */
#ifdef DEBUG

#define DEBUG_SAVE_ESP()                 \
   uptr curr_esp;                        \
   uptr saved_esp = get_stack_ptr();

#define DEBUG_CHECK_ESP()                                                 \
      curr_esp = get_stack_ptr();                                         \
      if (curr_esp != saved_esp)                                          \
         panic("ESP changed. Saved: %p, Curr: %p", saved_esp, curr_esp);

#else

#define DEBUG_SAVE_ESP()
#define DEBUG_CHECK_ESP()

#endif

void tasklet_runner_kthread()
{
   bool tasklet_run;

   DEBUG_SAVE_ESP()

   while (true) {

      DEBUG_CHECK_ESP()

      do {

         tasklet_run = run_one_tasklet();

      } while (tasklet_run);

#ifndef DEBUG
      kcond_wait(&tasklet_cond, NULL, TIMER_HZ / 10);
#else
      /*
       * In debug builds, use kernel_yield() in order to keep this task always
       * runnable and force the kernel to do much more context switches. This
       * helps some nasty bugs easier to reproduce.
       */
      kernel_yield();
#endif
   }
}
