
#include <common/string_util.h>
#include <exos/fault_resumable.h>
#include <exos/hal.h>
#include <exos/process.h>

void handle_resumable_fault(regs *r)
{
   task_info *curr = get_curr_task();

   ASSERT(!are_interrupts_enabled());
   pop_nested_interrupt(); // the fault
   set_return_register(curr->fault_resume_regs, 1 << regs_intnum(r));
   context_switch(curr->fault_resume_regs);
}

int asm_do_fault_resumable_call(u32 faults_mask, void *func, u32 *nargs_ref);

int fault_resumable_call(u32 faults_mask, void *func, u32 nargs, ...)
{
   int r;
   ASSERT(is_preemption_enabled());
   ASSERT(nargs <= 6);

   disable_preemption();
   {
      r = asm_do_fault_resumable_call(faults_mask, func, &nargs);
   }
   enable_preemption();
   return r;
}
