
#include <common/string_util.h>

#include <exos/process.h>
#include <exos/kmalloc.h>
#include <exos/tasklet.h>

//#define DEBUG_printk printk
#define DEBUG_printk(...)

void task_info_reset_kernel_stack(task_info *ti)
{
   uptr bottom = (uptr) ti->kernel_stack + KTHREAD_STACK_SIZE - 1;
   ti->kernel_state_regs = (regs *) (bottom & POINTER_ALIGN_MASK);
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
   push_on_user_stack(r, 0); // mandatory final NULL pointer

   for (int i = envc - 1; i >= 0; i--) {
      push_on_user_stack(r, env_pointers[i]);
   }

   // push the argv array (in reverse order)
   push_on_user_stack(r, 0); // mandatory final NULL pointer

   for (int i = argc - 1; i >= 0; i--) {
      push_on_user_stack(r, pointers[i]);
   }

   // push argc as last (since it will be the first to be pop-ed)
   push_on_user_stack(r, argc);
}

task_info *kthread_create(kthread_func_ptr fun, void *arg)
{
   regs r = {0};
   r.gs = r.fs = r.es = r.ds = r.ss = X86_KERNEL_DATA_SEL;
   r.cs = X86_KERNEL_CODE_SEL;

   r.eip = (u32) fun;
   r.eflags = 0x2 /* reserved, should be always set */ | EFLAGS_IF;

   task_info *ti = allocate_new_thread(kernel_process->pi);
   VERIFY(ti != NULL); // TODO: handle this

   ti->what = fun;
   ti->state = TASK_STATE_RUNNABLE;
   ti->running_in_kernel = 1;
   task_info_reset_kernel_stack(ti);

   push_on_stack((uptr **)&ti->kernel_state_regs, (uptr) arg);

   /*
    * Pushes the address of kthread_exit() into thread's stack in order to
    * it to be called after thread's function returns.
    * This is AS IF kthread_exit() called the thread 'fun' with a CALL
    * instruction before doing anything else. That allows the RET by 'fun' to
    * jump in the begging of kthread_exit().
    */

   push_on_stack((uptr **)&ti->kernel_state_regs, (uptr) &kthread_exit);

   /*
    * Overall, with these pushes + the iret of asm_kernel_context_switch_x86()
    * the stack will look to 'fun' as if the following happened:
    *
    *    kthread_exit:
    *       push arg
    *       call fun
    *       <other instructions of kthread_exit>
    */

   ti->kernel_state_regs = (void *)ti->kernel_state_regs - sizeof(regs) + 8;
   memcpy(ti->kernel_state_regs, &r, sizeof(r) - 8);

   add_task(ti);
   return ti;
}

void kthread_exit(void)
{
   disable_preemption();

   //printk("[kthread exit] tid: %i\n", current->tid);

   task_change_state(get_current_task(), TASK_STATE_ZOMBIE);

   /* WARNING: the following call discards the whole stack! */
   switch_to_initial_kernel_stack();

   /* Free the heap allocations used by the task, including the kernel stack */
   free_mem_for_zombie_task(get_current_task());

   /* Remove the from the scheduler and free its struct */
   remove_task(get_current_task());

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

   memcpy(&ti->state_regs, &r, sizeof(r));
   task_info_reset_kernel_stack(ti);
   return ti;
}

void save_current_task_state(regs *r)
{
   task_info *curr = get_current_task();
   ASSERT(curr != NULL);

   if (curr->running_in_kernel) {

      curr->kernel_state_regs = r;
      DEBUG_VALIDATE_STACK_PTR();

   } else {
      memcpy(&curr->state_regs, r, sizeof(*r));
   }
}

void panic_save_current_task_state(regs *r)
{
   if (!get_current_task()) {

      /*
       * PANIC occurred before the first task is started.
       * Set current = kernel_process to allow the rest of the panic code
       * to not handle the current == NULL case.
       */

      set_current_task(kernel_process);
   }

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
    * switch, just saving the ESP in kernel_state_regs won't work, because
    * we'll going to continue using the same stack. In this particular corner
    * case, just store the regs in state_regs and make kernel_state_regs point
    * there.
    */

   task_info *curr = get_current_task();
   memcpy(&curr->state_regs, r, sizeof(*r));
   curr->kernel_state_regs = &curr->state_regs;
}

/*
 * Sched functions that are here beacuse of arch-specific statements.
 */

void set_current_task_in_user_mode(void)
{
   ASSERT(!is_preemption_enabled());
   task_info *curr = get_current_task();

   curr->running_in_kernel = 0;

   task_info_reset_kernel_stack(curr);
   set_kernel_stack((u32)curr->kernel_state_regs);
}

#include "gdt_int.h"


NORETURN void switch_to_task(task_info *ti)
{
   ASSERT(!get_current_task() || get_current_task()->state != TASK_STATE_RUNNING);
   ASSERT(ti->state == TASK_STATE_RUNNABLE);
   ASSERT(ti != get_current_task());

   DEBUG_printk("[sched] Switching to tid: %i %s %s\n",
                ti->tid,
                is_kernel_thread(ti) ? "[KTHREAD]" : "[USER]",
                ti->running_in_kernel ? "(kernel mode)" : "(usermode)");

   task_change_state(ti, TASK_STATE_RUNNING);
   ti->time_slot_ticks = 0;

   if (get_curr_page_dir() != ti->pi->pdir) {
      set_page_directory(ti->pi->pdir);
   }

   disable_interrupts_forced();

#if KERNEL_TRACK_NESTED_INTERRUPTS
   pop_nested_interrupt();

   if (get_current_task()) {
      if (get_current_task()->running_in_kernel)
         if (!is_kernel_thread(get_current_task()))
            nested_interrupts_drop_top_syscall();
   }
#endif

   enable_preemption();
   ASSERT(is_preemption_enabled());

   DEBUG_VALIDATE_STACK_PTR();

   // We have to be SURE that the timer IRQ is NOT masked!
   irq_clear_mask(X86_PC_TIMER_IRQ);

   regs *state = ti->running_in_kernel
                  ? ti->kernel_state_regs
                  : &ti->state_regs;

   ASSERT(state->eflags & EFLAGS_IF);

   if (!ti->running_in_kernel) {

      task_info_reset_kernel_stack(ti);

   } else {

      if (!is_kernel_thread(ti)) {
         push_nested_interrupt(SYSCALL_SOFT_INTERRUPT);
      }

   }

   set_current_task(ti); /* this is safe here: the interrupts are disabled! */
   set_kernel_stack((u32) ti->kernel_state_regs);

   if (ti->ldt) {
      load_ldt(ti->ldt_index_in_gdt, ti->ldt_size);
   }

   context_switch(state);
}

sptr sys_set_tid_address(int *tidptr)
{
   get_current_task()->pi->tidptr = tidptr;
   return get_current_task()->tid;
}

void arch_specific_new_task_setup(task_info *ti)
{
   ti->ldt = NULL;
   ti->pi->tidptr = NULL;
   bzero(ti->gdt_entries, sizeof(ti->gdt_entries));
}

void arch_specific_free_task(task_info *ti)
{
   if (ti->ldt)
      gdt_clear_entry(ti->ldt_index_in_gdt);

   for (u32 i = 0; i < ARRAY_SIZE(ti->gdt_entries); i++)
      if (ti->gdt_entries[i])
         gdt_clear_entry(ti->gdt_entries[i]);
}
