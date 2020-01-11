/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/process_int.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/tasklet.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/signal.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/paging_hw.h>
#include <tilck/kernel/irq.h>

#include "gdt_int.h"

void soft_interrupt_resume(void);

//#define DEBUG_printk printk
#define DEBUG_printk(...)

STATIC_ASSERT(
   OFFSET_OF(struct task, fault_resume_regs) == TI_F_RESUME_RS_OFF
);
STATIC_ASSERT(
   OFFSET_OF(struct task, faults_resume_mask) == TI_FAULTS_MASK_OFF
);

STATIC_ASSERT(sizeof(struct task) + sizeof(struct process) <= 1024);

void task_info_reset_kernel_stack(struct task *ti)
{
   ulong bottom = (ulong)ti->kernel_stack + KERNEL_STACK_SIZE - 1;
   ti->state_regs = (regs_t *)(bottom & POINTER_ALIGN_MASK);
}

static inline void push_on_stack(ulong **stack_ptr_ref, ulong val)
{
   (*stack_ptr_ref)--;     // Decrease the value of the stack pointer
   **stack_ptr_ref = val;  // *stack_ptr = val
}

static void push_on_stack2(pdir_t *pdir, ulong **stack_ptr_ref, ulong val)
{
   // Decrease the value of the stack pointer
   (*stack_ptr_ref)--;

   // *stack_ptr = val
   debug_checked_virtual_write(pdir, *stack_ptr_ref, &val, sizeof(ulong));
}

static inline void push_on_user_stack(regs_t *r, ulong val)
{
   push_on_stack((ulong **)&r->useresp, val);
}

static void push_string_on_user_stack(regs_t *r, const char *str)
{
   const size_t len = strlen(str) + 1; // count also the '\0'
   const size_t aligned_len = round_down_at(len, sizeof(ulong));
   const size_t rem = len - aligned_len;

   r->useresp -= aligned_len + (rem > 0 ? sizeof(ulong) : 0);
   memcpy((void *)r->useresp, str, aligned_len);

   if (rem > 0) {
      ulong smallbuf = 0;
      memcpy(&smallbuf, str + aligned_len, rem);
      memcpy((void *)(r->useresp + aligned_len), &smallbuf, sizeof(smallbuf));
   }
}

static int
push_args_on_user_stack(regs_t *r,
                        const char *const *argv,
                        u32 argc,
                        const char *const *env,
                        u32 envc)
{
   ulong pointers[32];
   ulong env_pointers[96];

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
   push_on_user_stack(r, (ulong)argc);
   return 0;
}

NODISCARD int
kthread_create(kthread_func_ptr func, int fl, void *arg)
{
   struct task *ti;
   int tid, ret = -ENOMEM;

   regs_t r = {
      .kernel_resume_eip = (ulong)&soft_interrupt_resume,
      .custom_flags = 0,
      .gs = X86_KERNEL_DATA_SEL,
      .fs = X86_KERNEL_DATA_SEL,
      .es = X86_KERNEL_DATA_SEL,
      .ds = X86_KERNEL_DATA_SEL,
      .edi = 0, .esi = 0, .ebp = 0, .esp = 0,
      .ebx = 0, .edx = 0, .ecx = 0, .eax = 0,
      .int_num = 0,
      .err_code = 0,
      .eip = (ulong)func,
      .cs = X86_KERNEL_CODE_SEL,
      .eflags = 0x2 /* reserved, should be always set */ | EFLAGS_IF,
      .useresp = 0,
      .ss = X86_KERNEL_DATA_SEL,
   };

   disable_preemption();

   tid = create_new_kernel_tid();

   if (tid < 0) {
      ret = -EAGAIN;
      goto end;
   }

   ti = allocate_new_thread(kernel_process->pi, tid, !!(fl & KTH_ALLOC_BUFS));

   if (!ti)
      goto end;

   ASSERT(is_kernel_thread(ti));

   ti->what = func;
   ti->state = TASK_STATE_RUNNABLE;
   ti->running_in_kernel = true;
   task_info_reset_kernel_stack(ti);

   /*
    * 1) Push into the stack, function's argument, `arg`.
    *
    * 2) Push the address of kthread_exit() into thread's stack in order to it
    *    to be called after thread's function returns. It's AS IF kthread_exit
    *    called the thread `func` with a CALL instruction before doing anything
    *    else. That allows the RET by `func` to jump at the begging of
    *    kthread_exit().
    *
    * 3) Reserve space for the regs on the stack
    * 4) Copy the actual regs to the new stack
    */

   push_on_stack((ulong **)&ti->state_regs, (ulong)arg);
   push_on_stack((ulong **)&ti->state_regs, (ulong)&kthread_exit);
   ti->state_regs = (void *)ti->state_regs - sizeof(regs_t) + 8;
   memcpy(ti->state_regs, &r, sizeof(r) - 8);

   ret = ti->tid;

   /*
    * After the following call to add_task(), given that preemption is enabled,
    * there is NO GUARANTEE that the `tid` returned by this function will still
    * belong to a valid kernel thread. For example, the kernel thread might run
    * and terminate before the caller has the chance to run. Therefore, it is up
    * to the caller to be prepared for that.
    */

   add_task(ti);
   enable_preemption();

end:
   return ret; /* tid or error */
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

   wake_up_tasks_waiting_on(get_curr_task(), task_died);
   task_change_state(get_curr_task(), TASK_STATE_ZOMBIE);

   /* WARNING: the following call discards the whole stack! */
   switch_to_initial_kernel_stack();

   /* Free the heap allocations used by the task, including the kernel stack */
   free_mem_for_zombie_task(get_curr_task());

   /* Remove the from the scheduler and free its struct */
   remove_task(get_curr_task());

   {
      ulong var;
      disable_interrupts(&var);
      set_curr_task(kernel_process);
      enable_interrupts(&var);
   }
   schedule_outside_interrupt_context();
}

static void
setup_usermode_task_regs(regs_t *r, void *entry, void *stack_addr)
{
   *r = (regs_t) {
      .kernel_resume_eip = (ulong)&soft_interrupt_resume,
      .custom_flags = 0,
      .gs = X86_USER_DATA_SEL,
      .fs = X86_USER_DATA_SEL,
      .es = X86_USER_DATA_SEL,
      .ds = X86_USER_DATA_SEL,
      .edi = 0, .esi = 0, .ebp = 0, .esp = 0,
      .ebx = 0, .edx = 0, .ecx = 0, .eax = 0,
      .int_num = 0,
      .err_code = 0,
      .eip = (ulong)entry,
      .cs = X86_USER_CODE_SEL,
      .eflags = 0x2 /* reserved, should be always set */ | EFLAGS_IF,
      .useresp = (ulong)stack_addr,
      .ss = X86_USER_DATA_SEL,
   };
}

static int NO_INLINE
setup_usermode_task_first_process(pdir_t *pdir, struct task **ti_ref)
{
   struct task *ti;
   struct process *pi;

   VERIFY(create_new_pid() == 1);

   if (!(ti = allocate_new_process(kernel_process, 1, pdir)))
      return -ENOMEM;

   pi = ti->pi;
   pi->pgid = 1;
   pi->sid = 1;
   pi->umask = 0022;
   ti->state = TASK_STATE_RUNNABLE;
   add_task(ti);
   memcpy(pi->str_cwd, "/", 2);
   *ti_ref = ti;
   return 0;
}

int setup_usermode_task(pdir_t *pdir,
                        void *entry,
                        void *stack_addr,
                        struct task *ti,
                        const char *const *argv,
                        const char *const *env,
                        struct task **ti_ref)
{
   regs_t r;
   int rc = 0;
   u32 argv_elems = 0;
   u32 env_elems = 0;
   pdir_t *old_pdir;
   struct process *pi = NULL;

   ASSERT(!is_preemption_enabled());

   *ti_ref = NULL;
   setup_usermode_task_regs(&r, entry, stack_addr);

   /* Switch to the new page directory (we're going to write on user's stack) */
   old_pdir = get_curr_pdir();
   set_curr_pdir(pdir);

   while (argv[argv_elems]) argv_elems++;
   while (env[env_elems]) env_elems++;

   if ((rc = push_args_on_user_stack(&r, argv, argv_elems, env, env_elems)))
      goto err;

   if (UNLIKELY(!ti)) {

      /* Special case: applies only for `init`, the first process */

      if ((rc = setup_usermode_task_first_process(pdir, &ti)))
         goto err;

      ASSERT(ti != NULL);
      pi = ti->pi;

   } else {

      /*
       * Common case: we're creating a new process using the data structures
       * and the PID from a forked child (the `ti` task).
       */

      pi = ti->pi;
      remove_all_user_zero_mem_mappings(pi);
      remove_all_file_mappings(pi);
      process_free_mmap_heap(pi);
      arch_specific_free_task(ti);

      ASSERT(old_pdir == pi->pdir);
      pdir_destroy(pi->pdir);
      pi->pdir = pdir;
      old_pdir = NULL;

      arch_specific_new_task_setup(ti, NULL);

      ASSERT(ti->state == TASK_STATE_RUNNING);
      task_change_state(ti, TASK_STATE_RUNNABLE);
   }

   ti->running_in_kernel = false;
   ASSERT(ti->kernel_stack != NULL);

   task_info_reset_kernel_stack(ti);
   ti->state_regs--;    // make room for a regs_t struct in the stack
   *ti->state_regs = r; // copy the regs_t struct we just prepared
   *ti_ref = ti;
   return 0;

err:
   ASSERT(rc != 0);

   if (old_pdir) {
      set_curr_pdir(old_pdir);
      pdir_destroy(pdir);
   }

   return rc;
}

void save_current_task_state(regs_t *r)
{
   struct task *curr = get_curr_task();

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
   struct task *curr = get_curr_task();

   curr->running_in_kernel = false;

   task_info_reset_kernel_stack(curr);
   set_kernel_stack((u32)curr->state_regs);
}

static inline bool is_fpu_enabled_for_task(struct task *ti)
{
   return get_arch_fields(ti)->aligned_fpu_regs &&
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
switch_to_task_pop_nested_interrupts(int curr_int)
{
   if (KRN_TRACK_NESTED_INTERR) {

      ASSERT(get_curr_task() != NULL);

      if (curr_int != -1)
         pop_nested_interrupt();

      if (get_curr_task()->running_in_kernel)
         if (!is_kernel_thread(get_curr_task()))
            nested_interrupts_drop_top_syscall();
   }
}

static inline void
switch_to_task_clear_irq_mask(int curr_int)
{
   if (!is_irq(curr_int))
      return; /* Invalid IRQ#: nothing to do. NOTE: -1 is a special value */

   const int curr_irq = int_to_irq(curr_int);

   if (KRN_TRACK_NESTED_INTERR) {

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

NORETURN void switch_to_task(struct task *ti, int curr_int)
{
   /* Save the value of ti->state_regs as it will be reset below */
   regs_t *state = ti->state_regs;
   struct task *curr = get_curr_task();

   ASSERT(curr != NULL);
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

   if (!is_kernel_thread(curr) && curr->state != TASK_STATE_ZOMBIE)
      save_curr_fpu_ctx_if_enabled();

   if (!is_kernel_thread(ti)) {

      arch_task_members_t *arch = get_arch_fields(ti);

      /* Switch the page directory only if really necessary */
      if (get_curr_pdir() != ti->pi->pdir)
         set_curr_pdir(ti->pi->pdir);

      if (arch->ldt)
         load_ldt(arch->ldt_index_in_gdt, arch->ldt_size);

      if (is_fpu_enabled_for_task(ti)) {
         hw_fpu_enable();
         restore_fpu_regs(ti, false);
         /* leave FPU enabled */
      }
   }

   /* From here until the end, we have to be as fast as possible */
   disable_interrupts_forced();
   switch_to_task_pop_nested_interrupts(curr_int);
   enable_preemption();

   /*
    * Make sure in NO WAY we'll switch to a user task while keeping the
    * preemption disabled. That would be pretty bad.
    */
   ASSERT(is_preemption_enabled());
   DEBUG_VALIDATE_STACK_PTR();
   switch_to_task_clear_irq_mask(curr_int);

   if (!ti->running_in_kernel) {

      task_info_reset_kernel_stack(ti);

   } else {

      /*
       * The new task was running in kernel when it was preempted.
       *
       * In theory, there's nothing we have to do here, and that's exactly
       * what happens when KRN_TRACK_NESTED_INTERR is 0. But, our nice
       * debug feature for nested interrupts tracking requires a little work:
       * because of its assumptions (hard-coded in ASSERTS) are that when the
       * kernel is running, it's always inside some kind of interrupt handler
       * (fault, int 0x80 [syscall], IRQ) before resuming the next task, we have
       * to resume the state of the nested_interrupts in one case: the one when
       * we're resuming a USER task that was running in KERNEL MODE (the kernel
       * was running on behalf of the task). In that case, when for the first
       * time the user task got to the kernel, we had a nice 0x80 added in our
       * nested_interrupts array [even in the case of sysenter] by the function
       * soft_interrupt_entry(). The kernel started to work on behalf of the
       * user process but, for some reason (typically kernel preemption or
       * wait on condition) the task was scheduled out. When that happened,
       * because of the function switch_to_task_pop_nested_interrupts() called
       * above, the 0x80 value was dropped from `nested_interrupts`. Now that
       * we have to resume the execution of the user task (but in kernel mode),
       * we MUST push back that 0x80 in order to compensate the pop that will
       * occur in kernel's soft_interrupt_entry() just before returning back
       * to the user. That's because the nested_interrupts array is global and
       * not specific to any given task. Like the registers, it has to be saved
       * and restored in a consistent way.
       */

      if (!is_kernel_thread(ti)) {
         push_nested_interrupt(SYSCALL_SOFT_INTERRUPT);
      }
   }

   set_curr_task(ti);
   set_kernel_stack((u32)ti->state_regs);
   context_switch(state);
}

int sys_set_tid_address(int *tidptr)
{
   /*
    * NOTE: this syscall must always succeed. In case the user pointer
    * is not valid, we'll send SIGSEGV to the just created thread.
    */

   get_curr_proc()->set_child_tid = tidptr;
   return get_curr_task()->tid;
}

bool
arch_specific_new_task_setup(struct task *ti, struct task *parent)
{
   arch_task_members_t *arch = get_arch_fields(ti);

   if (LIKELY(parent != NULL)) {
      memcpy(&ti->arch_fields, &parent->arch_fields, sizeof(ti->arch_fields));
   }

   arch->aligned_fpu_regs = NULL;
   arch->fpu_regs_size = 0;

#if FORK_NO_COW

   if (LIKELY(!is_kernel_thread(ti))) {
      if (!allocate_fpu_regs(arch))
         return false; // out-of-memory
   }

#endif

   if (LIKELY(parent != NULL)) {

      if (arch->ldt)
         gdt_entry_inc_ref_count(arch->ldt_index_in_gdt);

      for (u32 i = 0; i < ARRAY_SIZE(arch->gdt_entries); i++)
         if (arch->gdt_entries[i])
            gdt_entry_inc_ref_count(arch->gdt_entries[i]);
   }

   ti->pi->set_child_tid = NULL;
   return true;
}

void arch_specific_free_task(struct task *ti)
{
   arch_task_members_t *arch = get_arch_fields(ti);

   if (arch->ldt) {
      gdt_clear_entry(arch->ldt_index_in_gdt);
      arch->ldt = NULL;
   }

   for (u32 i = 0; i < ARRAY_SIZE(arch->gdt_entries); i++) {
      if (arch->gdt_entries[i]) {
         gdt_clear_entry(arch->gdt_entries[i]);
         arch->gdt_entries[i] = 0;
      }
   }

   aligned_kfree2(arch->aligned_fpu_regs, arch->fpu_regs_size);
   arch->aligned_fpu_regs = NULL;
   arch->fpu_regs_size = 0;
}

/* General protection fault handler */
void handle_gpf(regs_t *r)
{
   if (!get_curr_task() || is_kernel_thread(get_curr_task()))
      panic("General protection fault. Error: %p\n", r->err_code);

   end_fault_handler_state();
   send_signal(get_curr_tid(), SIGSEGV, true);
}

/* Illegal instruction fault handler */
void handle_ill(regs_t *r)
{
   if (!get_curr_task() || is_kernel_thread(get_curr_task()))
      panic("Illegal instruction fault. Error: %p\n", r->err_code);

   end_fault_handler_state();
   send_signal(get_curr_tid(), SIGILL, true);
}

/* Division by zero fault handler */
void handle_div0(regs_t *r)
{
   if (!get_curr_task() || is_kernel_thread(get_curr_task()))
      panic("Division by zero fault. Error: %p\n", r->err_code);

   end_fault_handler_state();
   send_signal(get_curr_tid(), SIGFPE, true);
}

/* Coproc fault handler */
void handle_cpf(regs_t *r)
{
   if (!get_curr_task() || is_kernel_thread(get_curr_task()))
      panic("Co-processor (fpu) fault. Error: %p\n", r->err_code);

   end_fault_handler_state();
   send_signal(get_curr_tid(), SIGFPE, true);
}
