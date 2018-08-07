
#include <tilck/common/string_util.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/hal.h>

//#define DEBUG_printk printk
#define DEBUG_printk(...)

void task_info_reset_kernel_stack(task_info *ti)
{
   uptr bottom = (uptr) ti->kernel_stack + KTHREAD_STACK_SIZE - 1;
   ti->state_regs = (regs *) (bottom & POINTER_ALIGN_MASK);
}

static inline void push_on_stack(uptr **stack_ptr_ref, uptr val)
{
   (*stack_ptr_ref)--;     // Decrease the value of the stack pointer
   **stack_ptr_ref = val;  // *stack_ptr = val
}

static inline void push_on_user_stack(regs *r, uptr val)
{
   push_on_stack((uptr **)&r->useresp, val);
}

static void push_string_on_user_stack(regs *r, const char *str)
{
   size_t len = strlen(str) + 1; // count the '\0'
   size_t aligned_len = (len / sizeof(uptr)) * sizeof(uptr);

   size_t rem = len - aligned_len;
   r->useresp -= aligned_len + (rem > 0 ? sizeof(uptr) : 0);

   memcpy((void *)r->useresp, str, aligned_len);

   if (rem > 0) {
      uptr smallbuf = 0;
      memcpy(&smallbuf, str + aligned_len, rem);
      memcpy((void *)(r->useresp + aligned_len), &smallbuf, sizeof(smallbuf));
   }
}

static void push_args_on_user_stack(regs *r,
                                    char *const *argv,
                                    int argc,
                                    char *const *env,
                                    int envc)
{
   uptr pointers[argc]; // VLA
   uptr env_pointers[envc]; // VLA

   // push argv data on stack (it could be anywhere else, as well)
   for (int i = 0; i < argc; i++) {
      push_string_on_user_stack(r, argv[i]);
      pointers[i] = r->useresp;
   }

   // push env data on stack (it could be anywhere else, as well)
   for (int i = 0; i < envc; i++) {
      push_string_on_user_stack(r, env[i]);
      env_pointers[i] = r->useresp;
   }

   // push the env array (in reverse order)

   push_on_user_stack(r, 0); /*
                              * 2nd mandatory NULL pointer: after the 'env'
                              * pointers there could additional aux information
                              * that some libc implementations check for.
                              * Therefore, it is essential to add another NULL
                              * after the env pointers to inform the libc impl
                              * that no such information exist. For more info,
                              * check __init_libc() in libmusl.
                              */

   push_on_user_stack(r, 0); // mandatory final NULL pointer (end of 'env' ptrs)

   for (int i = envc - 1; i >= 0; i--) {
      push_on_user_stack(r, env_pointers[i]);
   }

   // push the argv array (in reverse order)
   push_on_user_stack(r, 0); // mandatory final NULL pointer (end of 'argv')

   for (int i = argc - 1; i >= 0; i--) {
      push_on_user_stack(r, pointers[i]);
   }

   // push argc as last (since it will be the first to be pop-ed)
   push_on_user_stack(r, argc);
}

NODISCARD task_info *
kthread_create(kthread_func_ptr fun, void *arg)
{
   regs r = {0};
   r.gs = r.fs = r.es = r.ds = r.ss = X86_KERNEL_DATA_SEL;
   r.cs = X86_KERNEL_CODE_SEL;

   r.eip = (u32) fun;
   r.eflags = 0x2 /* reserved, should be always set */ | EFLAGS_IF;

   task_info *ti = allocate_new_thread(kernel_process->pi);

   if (!ti)
      return NULL;

   ti->what = fun;
   ti->state = TASK_STATE_RUNNABLE;
   ti->running_in_kernel = 1;
   task_info_reset_kernel_stack(ti);

   push_on_stack((uptr **)&ti->state_regs, (uptr) arg);

   /*
    * Pushes the address of kthread_exit() into thread's stack in order to
    * it to be called after thread's function returns.
    * This is AS IF kthread_exit() called the thread 'fun' with a CALL
    * instruction before doing anything else. That allows the RET by 'fun' to
    * jump in the begging of kthread_exit().
    */

   push_on_stack((uptr **)&ti->state_regs, (uptr) &kthread_exit);

   /*
    * Overall, with these pushes + the iret of asm_kernel_context_switch_x86()
    * the stack will look to 'fun' as if the following happened:
    *
    *    kthread_exit:
    *       push arg
    *       call fun
    *       <other instructions of kthread_exit>
    */

   ti->state_regs = (void *)ti->state_regs - sizeof(regs) + 8;
   memcpy(ti->state_regs, &r, sizeof(r) - 8);

   add_task(ti);
   return ti;
}

void kthread_exit(void)
{
   task_info *pos;
   disable_preemption();

   list_for_each(pos, &sleeping_tasks_list, sleeping_list) {
      if (pos->wobj.ptr == get_curr_task()) {
         ASSERT(pos->wobj.type == WOBJ_TASK);
         wait_obj_reset(&pos->wobj);
         task_change_state(pos, TASK_STATE_RUNNABLE);
      }
   }

   task_change_state(get_curr_task(), TASK_STATE_ZOMBIE);

   /* WARNING: the following call discards the whole stack! */
   switch_to_initial_kernel_stack();

   /* Free the heap allocations used by the task, including the kernel stack */
   free_mem_for_zombie_task(get_curr_task());

   /* Remove the from the scheduler and free its struct */
   remove_task(get_curr_task());

   set_current_task(NULL);
   switch_to_idle_task_outside_interrupt_context();
}

task_info *create_usermode_task(page_directory_t *pdir,
                                void *entry,
                                void *stack_addr,
                                task_info *task_to_use,
                                char *const *argv,
                                char *const *env)
{
   size_t argv_elems = 0;
   size_t env_elems = 0;
   task_info *ti;
   regs r = {0};

   // User data GDT selector with bottom 2 bits set for ring 3.
   r.gs = r.fs = r.es = r.ds = r.ss = X86_USER_DATA_SEL;

   // User code GDT selector with bottom 2 bits set for ring 3.
   r.cs = X86_USER_CODE_SEL;

   r.eip = (u32) entry;
   r.useresp = (u32) stack_addr;

   while (argv[argv_elems]) argv_elems++;
   while (env[env_elems]) env_elems++;
   push_args_on_user_stack(&r, argv, argv_elems, env, env_elems);

   r.eflags = 0x2 /* reserved, always set */ | EFLAGS_IF;

   if (!task_to_use) {
      int pid = create_new_pid();
      VERIFY(pid != -1); // We CANNOT handle this.
      ti = allocate_new_process(NULL, pid);
      VERIFY(ti != NULL); // TODO: handle this
      ti->state = TASK_STATE_RUNNABLE;
      add_task(ti);
      memcpy(ti->pi->cwd, "/", 2);
   } else {
      ti = task_to_use;
      ASSERT(ti->state == TASK_STATE_RUNNABLE);
   }

   ti->pi->pdir = pdir;
   ti->running_in_kernel = false;

   ASSERT(ti->kernel_stack != NULL);

   task_info_reset_kernel_stack(ti);
   ti->state_regs--;    // make room for a regs struct in the stack
   *ti->state_regs = r; // copy the regs struct we just prepared

   return ti;
}

void save_current_task_state(regs *r)
{
   task_info *curr = get_curr_task();

   ASSERT(curr != NULL);
   curr->state_regs = r;
   DEBUG_VALIDATE_STACK_PTR();
}

void panic_save_current_task_state(regs *r)
{
   static regs panic_state_regs;

   /*
    * Clear the higher (unused) bits of the segment registers for a nicer
    * panic regs dump.
    */
   r->ss &= 0xffff;
   r->cs &= 0xffff;
   r->ds &= 0xffff;
   r->es &= 0xffff;
   r->fs &= 0xffff;
   r->gs &= 0xffff;

   /*
    * Since in panic we need just to save the state without doing a context
    * switch, just saving the ESP in state_regs won't work, because
    * we'll going to continue using the same stack. In this particular corner
    * case, just store the regs a static regs instance.
    */

   task_info *curr = get_curr_task();
   memcpy(&panic_state_regs, r, sizeof(regs));
   curr->state_regs = &panic_state_regs;
}

/*
 * Sched functions that are here beacuse of arch-specific statements.
 */

void set_current_task_in_user_mode(void)
{
   ASSERT(!is_preemption_enabled());
   task_info *curr = get_curr_task();

   curr->running_in_kernel = 0;

   task_info_reset_kernel_stack(curr);
   set_kernel_stack((u32)curr->state_regs);
}

#include "gdt_int.h"


NORETURN void switch_to_task(task_info *ti, int curr_irq)
{
   ASSERT(!get_curr_task() || get_curr_task()->state != TASK_STATE_RUNNING);
   ASSERT(ti->state == TASK_STATE_RUNNABLE);
   ASSERT(ti != get_curr_task());

   if (get_curr_task() && get_curr_task()->arch.fpu_regs) {
      hw_fpu_enable();
      {
         save_current_fpu_regs(false);
      }
      hw_fpu_disable();
   }

   DEBUG_printk("[sched] Switching to tid: %i %s %s\n",
                ti->tid,
                is_kernel_thread(ti) ? "[KTHREAD]" : "[USER]",
                ti->running_in_kernel ? "(kernel mode)" : "(usermode)");

   task_change_state(ti, TASK_STATE_RUNNING);
   ti->time_slot_ticks = 0;

   if (get_curr_pdir() != ti->pi->pdir) {
      set_page_directory(ti->pi->pdir);
   }

   disable_interrupts_forced(); /* IF = 0 before the context switch */

#if KERNEL_TRACK_NESTED_INTERRUPTS

   if (curr_irq != -1)
      pop_nested_interrupt();

   if (get_curr_task()) {
      if (get_curr_task()->running_in_kernel)
         if (!is_kernel_thread(get_curr_task()))
            nested_interrupts_drop_top_syscall();
   }
#endif

   enable_preemption();
   ASSERT(is_preemption_enabled());

   DEBUG_VALIDATE_STACK_PTR();

#if KERNEL_TRACK_NESTED_INTERRUPTS

   /*
    * When nested interrupts are tracked, nested IRQ #0 are allowed and in no
    * case the IRQ #0 is masked. Therefore there is no point in clearing the
    * IRQ mask if irq == 0, wasting a lot of cycles. On QEMU + KVM, the clear
    * mask function costs ~30K cycles, while on bare-metal costs 5-10 K cycles.
    */

   if (curr_irq > 0)
      irq_clear_mask(curr_irq);

#else

   /*
    * When nested interrupts are not tracked, nested IRQ #0 is not allowed.
    * Therefore here, as for any other IRQ, its mask has to be cleared.
    */
   if (curr_irq >= 0)
      irq_clear_mask(curr_irq);

#endif

   regs *state = ti->state_regs;
   ASSERT(state->eflags & EFLAGS_IF);

   if (!ti->running_in_kernel) {

      task_info_reset_kernel_stack(ti);

   } else {

      if (!is_kernel_thread(ti)) {
         push_nested_interrupt(SYSCALL_SOFT_INTERRUPT);
      }

   }

   set_current_task(ti); /* this is safe here: the interrupts are disabled! */
   set_kernel_stack((u32) ti->state_regs);

   if (ti->arch.ldt) {
      load_ldt(ti->arch.ldt_index_in_gdt, ti->arch.ldt_size);
   }

   if (ti->arch.fpu_regs) {
      hw_fpu_enable();
      restore_current_fpu_regs(false);
   }

   context_switch(state);
}

sptr sys_set_tid_address(int *tidptr)
{
   /*
    * NOTE: this syscall must always succeed. In case the user pointer
    * is not valid, we'll send SIGSEGV to the just created thread.
    */

   get_curr_task()->pi->set_child_tid = tidptr;
   return get_curr_task()->tid;
}

void arch_specific_new_task_setup(task_info *ti)
{
   ti->arch.ldt = NULL;
   ti->pi->set_child_tid = NULL;
   bzero(ti->arch.gdt_entries, sizeof(ti->arch.gdt_entries));
   ti->arch.fpu_regs = NULL;
}

void arch_specific_free_task(task_info *ti)
{
   if (ti->arch.ldt)
      gdt_clear_entry(ti->arch.ldt_index_in_gdt);

   for (u32 i = 0; i < ARRAY_SIZE(ti->arch.gdt_entries); i++)
      if (ti->arch.gdt_entries[i])
         gdt_clear_entry(ti->arch.gdt_entries[i]);

   kfree(ti->arch.fpu_regs);
}
