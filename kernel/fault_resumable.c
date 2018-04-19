
#include <common/string_util.h>
#include <exos/fault_resumable.h>
#include <exos/hal.h>
#include <exos/process.h>

volatile u32 __fault_resume_mask;
static u32 saved_disable_preemption_count;

/* This cannot be static since asm_do_fault_resumable_call() writes to it */
regs *saved_fault_resumable_regs;

void handle_resumable_fault(regs *r)
{
   ASSERT(!are_interrupts_enabled());
   pop_nested_interrupt(); // the fault
   disable_preemption_count = saved_disable_preemption_count;
   set_return_register(saved_fault_resumable_regs, 1 << regs_intnum(r));
   context_switch(saved_fault_resumable_regs);
}

int asm_do_fault_resumable_call(void *func, u32 *nargs_ref);

int fault_resumable_call(u32 faults_mask, void *func, u32 nargs, ...)
{
   int r;
   ASSERT(is_preemption_enabled());
   ASSERT(nargs <= 6);

   disable_preemption();
   {
      __fault_resume_mask = faults_mask;
      saved_disable_preemption_count = disable_preemption_count;
      r = asm_do_fault_resumable_call(func, &nargs);
      __fault_resume_mask = 0;
   }
   enable_preemption();
   return r;
}
