
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/tasklet.h>
#include <exos/kmalloc.h>
#include <exos/hal.h>
#include <exos/sync.h>
#include <exos/process.h>

task_info *__tasklet_runner_task;

typedef void (*tasklet_func)(uptr, uptr);

typedef struct {

   tasklet_func fptr;
   tasklet_context ctx;

} tasklet;

static tasklet *all_tasklets;
static kcond tasklet_cond;

typedef struct {

   union {

      struct {
         u32 used : 10;
         u32 read_pos : 10;
         u32 write_pos : 10;
         u32 unused : 2;
      };

      u32 raw;
   };

} ringbuf_stat;

static volatile ringbuf_stat tasklet_rbuf_stat;

bool any_tasklets_to_run(void)
{
   return tasklet_rbuf_stat.used != 0;
}

void init_tasklets(void)
{
   all_tasklets = kzmalloc(sizeof(tasklet) * MAX_TASKLETS);
   VERIFY(all_tasklets != NULL); // This cannot be handled.

   kcond_init(&tasklet_cond);

   __tasklet_runner_task = kthread_create(tasklet_runner_kthread, NULL);

#ifndef UNIT_TEST_ENVIRONMENT
   VERIFY(__tasklet_runner_task != NULL); // This cannot be handled.
#endif
}


bool enqueue_tasklet_int(void *func, uptr arg1, uptr arg2)
{
   ringbuf_stat cs, ns;

   tasklet new_tasklet = {
      .fptr = func,
      .ctx = {
         .arg1 = arg1,
         .arg2 = arg2
      }
   };

   disable_preemption();

   do {

      cs = tasklet_rbuf_stat;
      ns = tasklet_rbuf_stat;

      if (cs.used == MAX_TASKLETS) {
         enable_preemption();
         return false;
      }

      ns.used++;
      ns.write_pos = (ns.write_pos + 1) % MAX_TASKLETS;

   } while (!BOOL_COMPARE_AND_SWAP(&tasklet_rbuf_stat.raw, cs.raw, ns.raw));

   /*
    * We succeeded to reserve a slot for our new tasklet. Now we can safely
    * write the new_tasklet to its slot without worrying about racing with the
    * tasklet runner thread: preemption is disabled.
    */
   all_tasklets[cs.write_pos] = new_tasklet;


#ifndef UNIT_TEST_ENVIRONMENT

   /*
    * Special way of signalling a condition variable, without holding its lock:
    * this code will be often often called by higher-halfs of interrupt handlers
    * so it won't be possible to acquire a lock there. This means that the wait
    * on the other side may miss a signal (not waiting while we fire the signal
    * here) but that's OK since the tasklet runner thread calls run_one_tasklet
    * in a while(true) loop and it uses a timeout.
    */
   kcond_signal_single(&tasklet_cond, __tasklet_runner_task);

#endif


   enable_preemption();
   return true;
}

bool run_one_tasklet(void)
{
   ringbuf_stat cs, ns;
   tasklet tasklet_to_run;

   disable_preemption();

   do {
      cs = tasklet_rbuf_stat;
      ns = tasklet_rbuf_stat;

      if (!cs.used) {
         enable_preemption();
         return false;
      }

      tasklet_to_run = all_tasklets[cs.read_pos];

      ns.read_pos = (ns.read_pos + 1) % MAX_TASKLETS;
      ns.used--;

   } while (!BOOL_COMPARE_AND_SWAP(&tasklet_rbuf_stat.raw, cs.raw, ns.raw));

   enable_preemption();

   /* Run the tasklet with preemption enabled */
   tasklet_to_run.fptr(tasklet_to_run.ctx.arg1, tasklet_to_run.ctx.arg2);
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

void tasklet_runner_kthread(void)
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
