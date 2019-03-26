/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/string_util.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/process_int.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/signal.h>
#include <tilck/kernel/errno.h>

#include "gdt_int.h"

void soft_interrupt_resume(void);

//#define DEBUG_printk printk
#define DEBUG_printk(...)

void task_info_reset_kernel_stack(task_info *ti)
{
   uptr bottom = (uptr)ti->kernel_stack + KERNEL_STACK_SIZE - 1;
   ti->state_regs = (regs *)(bottom & POINTER_ALIGN_MASK);
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
   size_t len = strlen(str) + 1; // count also the '\0'
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

static int
push_args_on_user_stack(regs *r,
                        char *const *argv,
                        u32 argc,
                        char *const *env,
                        u32 envc)
{
   uptr pointers[32];
   uptr env_pointers[96];

   if (argc > ARRAY_SIZE(pointers))
      return -E2BIG;

   if (envc > ARRAY_SIZE(env_pointers))
      return -E2BIG;

   // push argv data on stack (it could be anywhere else, as well)
   for (u32 i = 0; i < argc; i++) {
      push_string_on_user_stack(r, argv[i]);
      pointers[i] = r->useresp;
   }

   // push env data on stack (it could be anywhere else, as well)
   for (u32 i = 0; i < envc; i++) {
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

   for (u32 i = envc; i > 0; i--) {
      push_on_user_stack(r, env_pointers[i - 1]);
   }

   // push the argv array (in reverse order)
   push_on_user_stack(r, 0); // mandatory final NULL pointer (end of 'argv')

   for (u32 i = argc; i > 0; i--) {
      push_on_user_stack(r, pointers[i - 1]);
   }

   // push argc as last (since it will be the first to be pop-ed)
   push_on_user_stack(r, (uptr)argc);
   return 0;
}

NODISCARD task_info *
kthread_create(kthread_func_ptr fun, void *arg)
{
   regs r = {
      .kernel_resume_eip = (uptr)&soft_interrupt_resume,
      .custom_flags = 0,
      .gs = X86_KERNEL_DATA_SEL,
      .fs = X86_KERNEL_DATA_SEL,
      .es = X86_KERNEL_DATA_SEL,
      .ds = X86_KERNEL_DATA_SEL,
      .edi = 0, .esi = 0, .ebp = 0, .esp = 0,
      .ebx = 0, .edx = 0, .ecx = 0, .eax = 0,
      .int_num = 0,
      .err_code = 0,
      .eip = (uptr)fun,
      .cs = X86_KERNEL_CODE_SEL,
      .eflags = 0x2 /* reserved, should be always set */ | EFLAGS_IF,
      .useresp = 0,
      .ss = X86_KERNEL_DATA_SEL
   };

   task_info *ti = allocate_new_thread(kernel_process->pi);

   if (!ti)
      return NULL;

   ASSERT(is_kernel_thread(ti));

   ti->what = fun;
   ti->state = TASK_STATE_RUNNABLE;
   ti->running_in_kernel = true;
   task_info_reset_kernel_stack(ti);

   push_on_stack((uptr **)&ti->state_regs, (uptr)arg);

   /*
    * Pushes the address of kthread_exit() into thread's stack in order to
    * it to be called after thread's function returns.
    * This is AS IF kthread_exit() called the thread 'fun' with a CALL
    * instruction before doing anything else. That allows the RET by 'fun' to
    * jump in the begging of kthread_exit().
    */

   push_on_stack((uptr **)&ti->state_regs, (uptr)&kthread_exit);

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
   /*
    * WARNING: DO NOT USE ANY STACK VARIABLES HERE.
    *
    * The call to switch_to_initial_kernel_stack() will mess-up your whole stack
    * (but that's what it is supposed to do). In this function, only global
    * variables can be accessed.
    *
    * This function gets called automatically when a kernel thread function
    * returns, but it can be called manually as well at any point.
    */
   disable_preemption();

   wake_up_tasks_waiting_on(get_curr_task());
   task_change_state(get_curr_task(), TASK_STATE_ZOMBIE);

   /* WARNING: the following call discards the whole stack! */
   switch_to_initial_kernel_stack();

   /* Free the heap allocations used by the task, including the kernel stack */
   free_mem_for_zombie_task(get_curr_task());

   /* Remove the from the scheduler and free its struct */
   remove_task(get_curr_task());

   set_curr_task(kernel_process);
   switch_to_idle_task_outside_interrupt_context();
}

int setup_usermode_task(page_directory_t *pdir,
                        void *entry,
                        void *stack_addr,
                        task_info *ti,
                        char *const *argv,
                        char *const *env,
                        task_info **ti_ref)
{
   regs r = {
      .kernel_resume_eip = (uptr)&soft_interrupt_resume,
      .custom_flags = 0,
      .gs = X86_USER_DATA_SEL,
      .fs = X86_USER_DATA_SEL,
      .es = X86_USER_DATA_SEL,
      .ds = X86_USER_DATA_SEL,
      .edi = 0, .esi = 0, .ebp = 0, .esp = 0,
      .ebx = 0, .edx = 0, .ecx = 0, .eax = 0,
      .int_num = 0,
      .err_code = 0,
      .eip = (uptr)entry,
      .cs = X86_USER_CODE_SEL,
      .eflags = 0x2 /* reserved, should be always set */ | EFLAGS_IF,
      .useresp = (uptr)stack_addr,
      .ss = X86_USER_DATA_SEL
   };

   int rc;
   u32 argv_elems = 0;
   u32 env_elems = 0;
   *ti_ref = NULL;

   while (argv[argv_elems]) argv_elems++;
   while (env[env_elems]) env_elems++;

   if ((rc = push_args_on_user_stack(&r, argv, argv_elems, env, env_elems)))
      return rc;

   if (UNLIKELY(!ti)) {

      /*
       * Special case: applies only for `init`, the first process.
       */

      VERIFY(create_new_pid() == 1);

      if (!(ti = allocate_new_process(kernel_process, 1)))
         return -ENOMEM;

      /*
       * The first process is created in SLEEPING state and remains in that
       * state until it's waken up at the end of init_drivers().
       */
      ti->state = TASK_STATE_SLEEPING;
      add_task(ti);
      memcpy(ti->pi->cwd, "/", 2);

   } else {

      /*
       * Common case: we're creating a new process using the data structures
       * and the PID from a forked child (the `ti` task).
       *
       * The only thing we HAVE TO do in this case is to free all GDT and LDT
       * entries by the current (forked) child since we're creating a totally
       * new process now.
       */
      arch_specific_free_task(ti);
      ASSERT(ti->state == TASK_STATE_RUNNABLE);
   }

   ti->pi->pdir = pdir;
   ti->running_in_kernel = false;

   ASSERT(ti->kernel_stack != NULL);

   task_info_reset_kernel_stack(ti);
   ti->state_regs--;    // make room for a regs struct in the stack
   *ti->state_regs = r; // copy the regs struct we just prepared
   *ti_ref = ti;
   return 0;
}

void save_current_task_state(regs *r)
{
   task_info *curr = get_curr_task();

   ASSERT(curr != NULL);
   curr->state_regs = r;
   DEBUG_VALIDATE_STACK_PTR();
}

/*
 * Sched functions that are here because of arch-specific statements.
 */

void set_current_task_in_user_mode(void)
{
   ASSERT(!is_preemption_enabled());
   task_info *curr = get_curr_task();

   curr->running_in_kernel = false;

   task_info_reset_kernel_stack(curr);
   set_kernel_stack((u32)curr->state_regs);
}

static inline bool is_fpu_enabled_for_task(task_info *ti)
{
   return ti->arch.fpu_regs &&
          (ti->state_regs->custom_flags & REGS_FL_FPU_ENABLED);
}

static inline void save_curr_fpu_ctx_if_enabled(void)
{
   if (is_fpu_enabled_for_task(get_curr_task())) {
      hw_fpu_enable();
      save_current_fpu_regs(false);
      hw_fpu_disable();
   }
}

static inline void
switch_to_task_pop_nested_interrupts(int curr_irq)
{
   if (KERNEL_TRACK_NESTED_INTERRUPTS) {

      ASSERT(get_curr_task() != NULL);

      if (curr_irq != -1)
         pop_nested_interrupt();

      if (get_curr_task()->running_in_kernel)
         if (!is_kernel_thread(get_curr_task()))
            nested_interrupts_drop_top_syscall();
   }
}

static inline void
switch_to_task_clear_irq_mask(int curr_irq)
{
   if (curr_irq < 0)
      return; /* Invalid IRQ#: nothing to do. NOTE: -1 is a special value */

   if (KERNEL_TRACK_NESTED_INTERRUPTS) {

      /*
       * When nested interrupts are tracked, nested IRQ #0 are allowed and in no
       * case the IRQ #0 is masked. Therefore there is no point in clearing the
       * IRQ mask if irq == 0, wasting a lot of cycles. On QEMU + KVM, the clear
       * mask function costs ~30K cycles, while on bare-metal costs 5-10 K
       * cycles.
       */

      if (curr_irq != X86_PC_TIMER_IRQ)
         irq_clear_mask(curr_irq);

   } else {

      /*
       * When nested interrupts are not tracked, nested IRQ #0 is not allowed.
       * Therefore here, as for any other IRQ, its mask has to be cleared.
       */

      irq_clear_mask(curr_irq);
   }
}

NORETURN void switch_to_task(task_info *ti, int curr_irq)
{
   /* Save the value of ti->state_regs as it will be reset below */
   regs *state = ti->state_regs;
   task_info *curr = get_curr_task();

   ASSERT(curr != NULL);
   ASSERT(ti != curr);
   ASSERT(curr->state != TASK_STATE_RUNNING);
   ASSERT(ti->state == TASK_STATE_RUNNABLE);
   ASSERT(!is_preemption_enabled());

   /*
    * Make sure in NO WAY we'll switch to a user task keeping interrupts
    * disabled. That would be a disaster.
    */
   ASSERT(state->eflags & EFLAGS_IF);

   /* Do as much as possible work before disabling the interrupts */
   task_change_state(ti, TASK_STATE_RUNNING);
   ti->time_slot_ticks = 0;

   if (!is_kernel_thread(curr))
      save_curr_fpu_ctx_if_enabled();

   if (!is_kernel_thread(ti)) {

      /* Switch the page directory only if really necessary */
      if (get_curr_pdir() != ti->pi->pdir)
         set_page_directory(ti->pi->pdir);

      if (ti->arch.ldt)
         load_ldt(ti->arch.ldt_index_in_gdt, ti->arch.ldt_size);

      if (is_fpu_enabled_for_task(ti)) {
         hw_fpu_enable();
         restore_fpu_regs(ti, false);
         /* leave FPU enabled */
      }
   }

   /* From here until the end, we have to be as fast as possible */
   disable_interrupts_forced();
   switch_to_task_pop_nested_interrupts(curr_irq);
   enable_preemption();

   /*
    * Make sure in NO WAY we'll switch to a user task keeping the preemption
    * disabled. That would be pretty bad.
    */
   ASSERT(is_preemption_enabled());
   DEBUG_VALIDATE_STACK_PTR();
   switch_to_task_clear_irq_mask(curr_irq);

   if (!ti->running_in_kernel) {

      task_info_reset_kernel_stack(ti);

   } else {

      if (!is_kernel_thread(ti)) {
         push_nested_interrupt(SYSCALL_SOFT_INTERRUPT);
      }
   }

   set_curr_task(ti);
   set_kernel_stack((u32)ti->state_regs);
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

bool arch_specific_new_task_setup(task_info *ti, task_info *parent)
{
   if (LIKELY(parent != NULL)) {
      memcpy(&ti->arch, &parent->arch, sizeof(ti->arch));
   }

   ti->arch.fpu_regs = NULL;
   ti->arch.fpu_regs_size = 0;

#if FORK_NO_COW

   if (LIKELY(!is_kernel_thread(ti))) {
      if (!allocate_fpu_regs(&ti->arch))
         return false; // out-of-memory
   }

#endif

   if (LIKELY(parent != NULL)) {

      if (ti->arch.ldt)
         gdt_entry_inc_ref_count(ti->arch.ldt_index_in_gdt);

      for (u32 i = 0; i < ARRAY_SIZE(ti->arch.gdt_entries); i++)
         if (ti->arch.gdt_entries[i])
            gdt_entry_inc_ref_count(ti->arch.gdt_entries[i]);
   }

   ti->pi->set_child_tid = NULL;
   return true;
}

void arch_specific_free_task(task_info *ti)
{
   if (ti->arch.ldt) {
      gdt_clear_entry(ti->arch.ldt_index_in_gdt);
      ti->arch.ldt = NULL;
   }

   for (u32 i = 0; i < ARRAY_SIZE(ti->arch.gdt_entries); i++) {
      if (ti->arch.gdt_entries[i]) {
         gdt_clear_entry(ti->arch.gdt_entries[i]);
         ti->arch.gdt_entries[i] = 0;
      }
   }

   kfree2(ti->arch.fpu_regs, ti->arch.fpu_regs_size);
   ti->arch.fpu_regs = NULL;
   ti->arch.fpu_regs_size = 0;
}

/* General protection fault handler */
void handle_gpf(regs *r)
{
   if (!get_curr_task() || is_kernel_thread(get_curr_task()))
      panic("General protection fault. Error: %p\n", r->err_code);

   end_fault_handler_state();
   send_signal(get_curr_task(), SIGSEGV);
}

/* Illegal instruction fault handler */
void handle_ill(regs *r)
{
   if (!get_curr_task() || is_kernel_thread(get_curr_task()))
      panic("Illegal instruction fault. Error: %p\n", r->err_code);

   end_fault_handler_state();
   send_signal(get_curr_task(), SIGILL);
}

/* Division by zero fault handler */
void handle_div0(regs *r)
{
   if (!get_curr_task() || is_kernel_thread(get_curr_task()))
      panic("Division by zero fault. Error: %p\n", r->err_code);

   end_fault_handler_state();
   send_signal(get_curr_task(), SIGFPE);
}

/* Coproc fault handler */
void handle_cpf(regs *r)
{
   if (!get_curr_task() || is_kernel_thread(get_curr_task()))
      panic("Co-processor (fpu) fault. Error: %p\n", r->err_code);

   end_fault_handler_state();
   send_signal(get_curr_task(), SIGFPE);
}
