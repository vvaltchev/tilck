/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_mm.h>
#include <tilck_gen_headers/config_debug.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/utils.h>
#include <tilck/common/unaligned.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/process_int.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/worker_thread.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/signal.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/paging_hw.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/vdso.h>
#include <tilck/kernel/switch.h>

#include <tilck/mods/tracing.h>

#include <linux/auxvec.h>

void asm_trap_entry_resume(void);

STATIC_ASSERT(
   OFFSET_OF(struct task, fault_resume_regs) == TI_F_RESUME_RS_OFF
);
STATIC_ASSERT(
   OFFSET_OF(struct task, faults_resume_mask) == TI_FAULTS_MASK_OFF
);

STATIC_ASSERT(sizeof(struct task_and_process) <= 2048);

int
push_args_on_user_stack(regs_t *r,
                        const char *const *argv,
                        u32 argc,
                        const char *const *env,
                        u32 envc)
{
   ulong pointers[32];
   ulong env_pointers[96];
   ulong aligned_len, len, rem;

   if (argc > ARRAY_SIZE(pointers))
      return -E2BIG;

   if (envc > ARRAY_SIZE(env_pointers))
      return -E2BIG;

   // push argv data on stack (it could be anywhere else, as well)
   for (u32 i = 0; i < argc; i++) {
      push_string_on_user_stack(r, READ_PTR(&argv[i]));
      pointers[i] = regs_get_usersp(r);
   }

   // push env data on stack (it could be anywhere else, as well)
   for (u32 i = 0; i < envc; i++) {
      push_string_on_user_stack(r, READ_PTR(&env[i]));
      env_pointers[i] = regs_get_usersp(r);
   }

   // make stack pointer align to 16 bytes

   len = (
      2 + // AT_NULL vector
      2 + // AT_PAGESZ vector
      1 + // mandatory final NULL pointer (end of 'env' ptrs)
      envc +
      1 + // mandatory final NULL pointer (end of 'argv')
      argc +
      1   // push argc as last (since it will be the first to be pop-ed)
   ) * sizeof(ulong);
   aligned_len = round_up_at(len, USERMODE_STACK_ALIGN);
   rem = aligned_len - len;

   for (u32 i = 0; i < rem / sizeof(ulong); i++) {
      push_on_user_stack(r, 0);
   }

   // push the aux array (in reverse order)

   push_on_user_stack(r, AT_NULL); // AT_NULL vector
   push_on_user_stack(r, 0);

   push_on_user_stack(r, PAGE_SIZE); // AT_PAGESZ vector
   push_on_user_stack(r, AT_PAGESZ);

   // push the env array (in reverse order)

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

int setup_sig_handler(struct task *ti,
                      enum sig_state sig_state,
                      regs_t *r,
                      ulong user_func,
                      int signum)
{
   if (ti->nested_sig_handlers == 0) {

      int rc;

      if (sig_state == sig_pre_syscall)
         r->a0 = (ulong) -EINTR;

      if ((rc = save_regs_on_user_stack(r)) < 0)
         return rc;
   }

   regs_set_ip(r, user_func);
   regs_set_usersp(r,
                   regs_get_usersp(r) -
                   SIG_HANDLER_ALIGN_ADJUST -
                   sizeof(ulong));
   set_return_register(r, signum);
   set_return_addr(r, post_sig_handler_user_vaddr);
   ti->nested_sig_handlers++;

   ASSERT((regs_get_usersp(r) & (USERMODE_STACK_ALIGN - 1)) == 0);
   return 0;
}

NODISCARD int
kthread_create2(kthread_func_ptr func, const char *name, int fl, void *arg)
{
   struct task *ti;
   int tid, ret = -ENOMEM;
   ASSERT(name != NULL);

   regs_t r =  {
      .kernel_resume_pc = (ulong)&asm_trap_entry_resume,
      .sepc = (ulong)func,
      .sstatus = SR_SPIE | SR_SPP | SR_SIE | SR_SUM,
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

   if (*name == '&')
      name++;         /* see the macro kthread_create() */

   ti->kthread_name = name;
   ti->state = TASK_STATE_RUNNABLE;
   ti->running_in_kernel = true;
   task_info_reset_kernel_stack(ti);

   r.a0 = (ulong)arg;
   r.ra = (ulong)&kthread_exit;
   r.sp = (ulong)ti->state_regs;
   ti->state_regs = (void *)ti->state_regs - sizeof(regs_t);
   memcpy(ti->state_regs, &r, sizeof(r));

   ret = ti->tid;

   if (fl & KTH_WORKER_THREAD)
      ti->worker_thread = arg;

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

void
setup_usermode_task_regs(regs_t *r, void *entry, void *stack_addr)
{
   *r = (regs_t) {
      .kernel_resume_pc = (ulong)&asm_trap_entry_resume,
      .sepc = (ulong)entry,
      .sp = 0,
      .usersp = (ulong)stack_addr,
      .sstatus = (ulong)SR_SPIE | SR_SUM, /* User mode, enable interrupt */
   };
}

/*
 * Sched functions that are here because of arch-specific statements.
 */

void
set_current_task_in_user_mode(void)
{
   ASSERT(!is_preemption_enabled());
   struct task *curr = get_curr_task();

   curr->running_in_kernel = false;
   task_info_reset_kernel_stack(curr);
}

static inline bool
is_fpu_enabled_for_task(struct task *ti)
{
   return get_task_arch_fields(ti)->fpu_regs &&
          (ti->state_regs->sstatus & SR_FS);
}

static inline void
save_curr_fpu_ctx_if_enabled(void)
{
   if (is_fpu_enabled_for_task(get_curr_task())) {
      save_current_fpu_regs(false);
   }
}

static void
switch_to_task_safety_checks(struct task *curr, struct task *next)
{
   static bool first_task_switch_passed;

   /*
    * Generally, we don't support task switches with interrupts disabled
    * simply because the current task might have ended up in the scheduler
    * by mistake, while doing a critical operation. That looks weird, but
    * why not checking against that? We have so far only *ONE* legit case
    * where entering in switch_to_task() is intentional: the first task
    * switch in kmain() to the init processs.
    *
    * In case it turns out that there are more *legit* cases where we need
    * switch to a new task with interrupts disabled, we might fix those cases
    * or decide to support that use-case, by replacing the checks below with
    * forced setting of the EFLAGS_IF bit:
    *
    *    state->eflags |= EFLAGS_IF
    *
    * For the moment, that is not necessary.
    */
   if (UNLIKELY(!are_interrupts_enabled())) {

      /*
       * Interrupts are disabled in this corner case: it's totally safe to read
       * and write the static boolean.
       */
      if (!first_task_switch_passed) {

         first_task_switch_passed = true;

      } else {

         /*
          * Oops! We're not in the first task switch and interrupts are
          * disabled: very likely there's a bug!
          */
         panic("Cannot switch away from task with interrupts disabled");
      }
   }

   /*
    * Make sure in NO WAY we'll switch to a user task keeping interrupts
    * disabled. That would be a disaster. And if that happens due to a weird
    * bug, let's try to learn as much as possible about why that happened.
    */
   if (UNLIKELY(!(next->state_regs->sstatus & SR_SIE) &&
                !(next->state_regs->sstatus & SR_SPIE))) {

      const char *curr_str =
         curr->kthread_name
            ? curr->kthread_name
            : curr->pi->debug_cmdline;

      const char *next_str =
         next->kthread_name
            ? next->kthread_name
            : next->pi->debug_cmdline;

      printk("[sched] task: %d (%p, %s) => %d (%p, %s)\n",
             curr->tid, curr, curr_str,
             next->tid, next, next_str);

      if (next->running_in_kernel) {
         dump_stacktrace(
            regs_get_frame_ptr(next->state_regs),
            next->pi->pdir
         );
      }

      panic("[sched] Next task does not have interrupts enabled. "
            "In kernel: %u, timer_ready: %u, is_sigsuspend: %u, "
            "sa_pending: %p, sa_fault_pending: %p, "
            "sa_mask: %p, sa_old_mask: %p",
            next->running_in_kernel,
            next->timer_ready,
            next->in_sigsuspend,
            next->sa_pending[0],
            next->sa_fault_pending[0],
            next->sa_mask[0],
            next->sa_old_mask[0]);
   }
}

NORETURN void
switch_to_task(struct task *ti)
{
   /* Save the value of ti->state_regs as it will be reset below */
   regs_t *state = ti->state_regs;
   struct task *curr = get_curr_task();

   ASSERT(curr != NULL);

   if (UNLIKELY(ti != curr)) {
      ASSERT(curr->state != TASK_STATE_RUNNING);
      ASSERT_TASK_STATE(ti->state, TASK_STATE_RUNNABLE);
   }

   ASSERT(!is_preemption_enabled());
   switch_to_task_safety_checks(curr, ti);

   /* Do as much as possible work before disabling the interrupts */
   task_change_state_idempotent(ti, TASK_STATE_RUNNING);
   ti->ticks.timeslice = 0;

   if (!is_kernel_thread(curr) && curr->state != TASK_STATE_ZOMBIE)
      save_curr_fpu_ctx_if_enabled();

   if (!is_kernel_thread(ti)) {

      if (get_curr_pdir() != ti->pi->pdir) {
         set_curr_pdir(ti->pi->pdir);
      }

      if (!ti->running_in_kernel && !(state->sstatus & SR_SPP))
         process_signals(ti, sig_in_usermode, state);

      if (is_fpu_enabled_for_task(ti)) {
         restore_fpu_regs(ti, false);
      }
   }

   /* From here until the end, we have to be as fast as possible */
   disable_interrupts_forced();
   switch_to_task_pop_nested_interrupts();
   enable_preemption_nosched();
   ASSERT(is_preemption_enabled());

   if (!ti->running_in_kernel)
      task_info_reset_kernel_stack(ti);
   else
      adjust_nested_interrupts_for_task_in_kernel(ti);

   set_curr_task(ti);
   ti->timer_ready = false;

   context_switch(state);
}

int
sys_set_tid_address(int *tidptr)
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
   arch_task_members_t *arch = get_task_arch_fields(ti);

   if (FORK_NO_COW) {

      if (parent) {

         /*
          * We parent is set, we're forking a task and we must NOT preserve the
          * arch fields. But, if we're not forking (parent is set), it means
          * we're in execve(): in that case there's no point to reset the arch
          * fields. Actually, here, in the NO_COW case, we MUST NOT do it, in
          * order to be sure we won't fail.
          */

         bzero(arch, sizeof(arch_task_members_t));
      }

      if (arch->fpu_regs) {

         /*
          * We already have an FPU regs buffer: just clear its contents and
          * keep it allocated.
          */
         bzero(arch->fpu_regs, arch->fpu_regs_size);

      } else {

         /* We don't have a FPU regs buffer: unless this is kthread, allocate */
         if (LIKELY(!is_kernel_thread(ti)))
            if (!allocate_fpu_regs(arch))
               return false; // out-of-memory
      }

   } else {

      /*
       * We're not in the NO_COW case. We have to free the arch specific fields
       * (like the fpu_regs buffer) if the parent is NULL. Otherwise, just reset
       * its members to zero.
       */

      if (parent) {
         bzero(arch, sizeof(*arch));
      } else {
         arch_specific_free_task(ti);
      }
   }

   return true;
}

void
arch_specific_free_task(struct task *ti)
{
   arch_task_members_t *arch = get_task_arch_fields(ti);
   kfree2(arch->fpu_regs, arch->fpu_regs_size);
   arch->fpu_regs = NULL;
   arch->fpu_regs_size = 0;
}

void
arch_specific_new_proc_setup(struct process *pi, struct process *parent)
{
   if (!parent)
      return;      /* we're done */

   pi->set_child_tid = NULL;
}

void
arch_specific_free_proc(struct process *pi)
{
   /* do nothing */
   return;
}

static void
handle_fatal_error(regs_t *r, int signum)
{
   send_signal(get_curr_tid(), signum, SIG_FL_PROCESS | SIG_FL_FAULT);
}

/* Access fault handler */
void handle_generic_fault_int(regs_t *r, const char *fault_name)
{
   if (!get_curr_task() || is_kernel_thread(get_curr_task()))
      panic("FAULT. Error: %s\n", fault_name);

   handle_fatal_error(r, SIGSEGV);
}

/* Instruction illegal fault handler */
void handle_inst_illegal_fault_int(regs_t *r, const char *fault_name)
{
   if (!get_curr_task() || is_kernel_thread(get_curr_task()))
      panic("FAULT. Error: %s\n", fault_name);

   handle_fatal_error(r, SIGILL);
}

/* Misaligned fault handler */
void handle_bus_fault_int(regs_t *r, const char *fault_name)
{
   if (!get_curr_task() || is_kernel_thread(get_curr_task()))
      panic("FAULT. Error: %s\n", fault_name);

   handle_fatal_error(r, SIGBUS);
}

