
#include <tasklet.h>
#include <kmalloc.h>
#include <string_util.h>
#include <hal.h>


typedef void (*tasklet_func)(uptr, uptr, uptr);

typedef struct {

   tasklet_func fptr;
   tasklet_context ctx;

} tasklet;


tasklet *all_tasklets = NULL;
static volatile int first_free_slot_index = 0;
static volatile int slots_used = 0;
static volatile int tasklet_to_execute = 0;

void initialize_tasklets()
{
   all_tasklets = kmalloc(sizeof(tasklet) * MAX_TASKLETS);

   ASSERT(all_tasklets != NULL);
   bzero(all_tasklets, sizeof(tasklet) * MAX_TASKLETS);
}


bool add_tasklet_int(void *func, uptr arg1, uptr arg2, uptr arg3)
{
   if (slots_used >= MAX_TASKLETS) {
      return false;
   }

   disable_preemption();
   {
      ASSERT(all_tasklets[first_free_slot_index].fptr == NULL);

      all_tasklets[first_free_slot_index].fptr = (tasklet_func)func;
      all_tasklets[first_free_slot_index].ctx.arg1 = arg1;
      all_tasklets[first_free_slot_index].ctx.arg2 = arg2;
      all_tasklets[first_free_slot_index].ctx.arg3 = arg3;

      first_free_slot_index = (first_free_slot_index + 1) % MAX_TASKLETS;
      slots_used++;
   }
   enable_preemption();

   return true;
}

bool run_one_tasklet()
{
   tasklet t;

   if (slots_used == 0) {
      return false;
   }

   disable_preemption();
   {
      ASSERT(all_tasklets[tasklet_to_execute].fptr != NULL);

      memmove(&t, &all_tasklets[tasklet_to_execute], sizeof(tasklet));
      all_tasklets[tasklet_to_execute].fptr = NULL;

      slots_used--;
      tasklet_to_execute = (tasklet_to_execute + 1) % MAX_TASKLETS;
   }
   enable_preemption();


   /* Execute the tasklet with preemption ENABLED */
   t.fptr(t.ctx.arg1, t.ctx.arg2, t.ctx.arg3);

   return true;
}
