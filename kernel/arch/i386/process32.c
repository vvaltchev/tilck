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

#include <tilck/mods/tracing.h>

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

STATIC_ASSERT(TOT_PROC_AND_TASK_SIZE <= 1024);

void task_info_reset_kernel_stack(struct task *ti)
{
   ASSERT(ti->kernel_stack != NULL);

   ulong stack_ptr = (ulong)ti->kernel_stack;
   ulong stack_bottom = stack_ptr + KERNEL_STACK_SIZE - 1;
   ASSERT(stack_bottom > stack_ptr);

   ti->state_regs = (regs_t *)(stack_bottom & POINTER_ALIGN_MASK);
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
      push_string_on_user_stack(r, READ_PTR(&argv[i]));
      pointers[i] = r->useresp;
   }

   // push env data on stack (it could be anywhere else, as well)
   for (u32 i = 0; i < envc; i++) {
      push_string_on_user_stack(r, READ_PTR(&env[i]));
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

static int save_regs_on_user_stack(regs_t *r)
{
   ulong new_useresp = r->useresp;
   int rc;

   /* Align the user ESP */
   new_useresp &= ALIGNED_MASK(USERMODE_STACK_ALIGN);

   /* Allocate space on the user stack */
   new_useresp -= sizeof(*r);

   /* Save the registers to the user stack */
   rc = copy_to_user(TO_PTR(new_useresp), r, sizeof(*r));

   if (rc) {
      /* Oops, stack overflow */
      return -EFAULT;
   }

   /* Now, after we saved the registers, update r->useresp */
   r->useresp = new_useresp;
   return 0;
}

static void restore_regs_from_user_stack(regs_t *r)
{
   ulong old_regs = r->useresp;
   int rc;

   /* Restore the registers we previously changed */
   rc = copy_from_user(r, TO_PTR(old_regs), sizeof(*r));

   if (rc) {
      /* Oops, something really weird happened here */
      enable_preemption();
      terminate_process(0, SIGSEGV);
      NOT_REACHED();
   }

   /* Don't trust user space */
   r->cs = X86_USER_CODE_SEL;
   r->eflags |= EFLAGS_IF;
}

void setup_pause_trampoline(regs_t *r)
{
   r->eip = pause_trampoline_user_vaddr;
}

/* See the comments below in setup_sig_handler() */
#define SIG_HANDLER_ALIGN_ADJUST                        \
   (                                                    \
      (                                                 \
         + USERMODE_STACK_ALIGN                         \
         - sizeof(regs_t)               /* regs */      \
         - sizeof(ulong)                /* signum */    \
      ) % USERMODE_STACK_ALIGN                          \
   )

int setup_sig_handler(struct task *ti,
                      enum sig_state sig_state,
                      regs_t *r,
                      ulong user_func,
                      int signum)
{
   if (ti->nested_sig_handlers == 0) {

      int rc;

      if (sig_state == sig_pre_syscall)
         r->eax = (ulong) -EINTR;

      if ((rc = save_regs_on_user_stack(r)) < 0)
         return rc;
   }

   r->eip = user_func;
   r->useresp -= SIG_HANDLER_ALIGN_ADJUST;
   push_on_user_stack(r, (ulong)signum);
   push_on_user_stack(r, post_sig_handler_user_vaddr);
   ti->nested_sig_handlers++;

   /*
    * Check that the stack pointer + 4 is aligned at a 16-bytes boundary.
    * The reason for that +4 (word size) is that the stack must be aligned
    * BEFORE the call instruction, not after it. So, at the first instruction,
    * the callee will see its ESP in hex ending with a "c", like this:
    *
    *    0xbfffce2c             # if we add +4, it's aligned at 16
    *
    * and NOT like this:
    *
    *    0xbfffce20             # it's already aligned at 16
    */
   ASSERT(((r->useresp + sizeof(ulong)) & (USERMODE_STACK_ALIGN - 1)) == 0);
   return 0;
}

ulong sys_rt_sigreturn(void)
{
   ASSERT(!is_preemption_enabled()); /* Thanks to SYSFL_NO_PREEMPT */
   struct task *curr = get_curr_task();
   regs_t *r = curr->state_regs;

   if (LIKELY(curr->nested_sig_handlers > 0)) {

      trace_printk(10, "Done running signal handler");

      r->useresp +=
         sizeof(ulong)               /* compensate the "push signum" above    */
         + SIG_HANDLER_ALIGN_ADJUST; /* compensate the forced stack alignment */

      if (!process_signals(curr, sig_in_return, r)) {

         if (curr->in_sigsuspend) {
            memcpy(curr->sa_mask, curr->sa_old_mask, sizeof(curr->sa_mask));
            curr->in_sigsuspend = false;
         }

         restore_regs_from_user_stack(r);
      }

      curr->nested_sig_handlers--;
      ASSERT(curr->nested_sig_handlers >= 0);

   } else {

      /* An user process tried to call directly rt_sigreturn() */
      r->eax = (ulong) -ENOSYS;
   }

   /*
    * NOTE: we must return r->eax because syscalls are called by handle_syscall
    * in a generic way like:
    *
    *     r->eax = (ulong) fptr(...)
    *
    * Returning anything else than r->eax would change that register and we
    * don't wanna do that in a special NORETURN function such this. Here we're
    * supposed to restore all the user registers as they were before the signal
    * handler ran. Failing to do that, has an especially visible effect when
    * a signal handler run after preempting running code in userspace: in that
    * case, no syscall was made and no register is expected to ever change,
    * exactly like in context switch.
    */
   return r->eax;
}

NODISCARD int
kthread_create2(kthread_func_ptr func, const char *name, int fl, void *arg)
{
   struct task *ti;
   int tid, ret = -ENOMEM;
   ASSERT(name != NULL);

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

   if (*name == '&')
      name++;         /* see the macro kthread_create() */

   ti->kthread_name = name;
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

   /* Run the scheduler */
   do_schedule();

   /* The scheduler will never return! */
   NOT_REACHED();
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
setup_first_process(pdir_t *pdir, struct task **ti_ref)
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
   ti->state = TASK_STATE_RUNNING;
   add_task(ti);
   memcpy(pi->str_cwd, "/", 2);
   *ti_ref = ti;
   return 0;
}

void
finalize_usermode_task_setup(struct task *ti, regs_t *user_regs)
{
   ASSERT(!is_preemption_enabled());

   ASSERT_TASK_STATE(ti->state, TASK_STATE_RUNNING);
   task_change_state(ti, TASK_STATE_RUNNABLE);

   ti->running_in_kernel = false;
   ASSERT(ti->kernel_stack != NULL);

   task_info_reset_kernel_stack(ti);
   ti->state_regs--;             // make room for a regs_t struct in the stack
   *ti->state_regs = *user_regs; // copy the regs_t struct we prepared before
}

int setup_process(struct elf_program_info *pinfo,
                  struct task *ti,
                  const char *const *argv,
                  const char *const *env,
                  struct task **ti_ref,
                  regs_t *r)
{
   int rc = 0;
   u32 argv_elems = 0;
   u32 env_elems = 0;
   pdir_t *old_pdir;
   struct process *pi = NULL;

   ASSERT(!is_preemption_enabled());

   *ti_ref = NULL;
   setup_usermode_task_regs(r, pinfo->entry, pinfo->stack);

   /* Switch to the new page directory (we're going to write on user's stack) */
   old_pdir = get_curr_pdir();
   set_curr_pdir(pinfo->pdir);

   while (READ_PTR(&argv[argv_elems])) argv_elems++;
   while (READ_PTR(&env[env_elems])) env_elems++;

   if ((rc = push_args_on_user_stack(r, argv, argv_elems, env, env_elems)))
      goto err;

   if (UNLIKELY(!ti)) {

      /* Special case: applies only for `init`, the first process */

      if ((rc = setup_first_process(pinfo->pdir, &ti)))
         goto err;

      ASSERT(ti != NULL);
      pi = ti->pi;

   } else {

      /*
       * Common case: we're creating a new process using the data structures
       * and the PID from a forked child (the `ti` task).
       */

      pi = ti->pi;

      if (pi->vforked) {

        /*
         * In case of vforked processes, we cannot remove any mappings and we
         * need some special management for the mappings info object (pi->mi).
         */
         vforked_child_transfer_dispose_mi(pi);

      } else {

         remove_all_user_zero_mem_mappings(pi);
         remove_all_file_mappings(pi);
         process_free_mappings_info(pi);

         ASSERT(old_pdir == pi->pdir);
         pdir_destroy(pi->pdir);

         if (pi->elf)
            release_subsys_flock(pi->elf);
      }

      pi->pdir = pinfo->pdir;
      old_pdir = NULL;

      /* NOTE: not calling arch_specific_free_task() */
      VERIFY(arch_specific_new_task_setup(ti, NULL));

      arch_specific_free_proc(pi);
      arch_specific_new_proc_setup(pi, NULL);
   }

   pi->elf = pinfo->lf;
   *ti_ref = ti;
   return 0;

err:
   ASSERT(rc != 0);

   if (old_pdir) {
      set_curr_pdir(old_pdir);
      pdir_destroy(pinfo->pdir);
   }

   return rc;
}

void save_current_task_state(regs_t *r, bool irq)
{
   struct task *curr = get_curr_task();

   ASSERT(curr != NULL);
   ASSERT(r != NULL);

   if (irq) {
      /*
       * In case of preemption while in userspace that happens while the
       * interrupts are disabled. Make sure we ignore that fact while saving
       * the current state and always keep the IF flag set in the EFLAGS
       * register.
       */
      r->eflags |= EFLAGS_IF;
   }

   curr->state_regs = r;
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
   set_kernel_stack((u32)curr->state_regs);
}

static inline bool
is_fpu_enabled_for_task(struct task *ti)
{
   return get_task_arch_fields(ti)->aligned_fpu_regs &&
          (ti->state_regs->custom_flags & REGS_FL_FPU_ENABLED);
}

static inline void
save_curr_fpu_ctx_if_enabled(void)
{
   if (is_fpu_enabled_for_task(get_curr_task())) {
      hw_fpu_enable();
      save_current_fpu_regs(false);
      hw_fpu_disable();
   }
}

static inline void
adjust_nested_interrupts_for_task_in_kernel(struct task *ti)
{
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
    * syscall_entry(). The kernel started to work on behalf of the user process
    * but, for some reason (typically kernel preemption or wait on condition)
    * the task was scheduled out. When that happened, the 0x80 value was dropped
    * from `nested_interrupts`. Now that we have to resume the execution of the
    * user task (but in kernel mode), we MUST push back that 0x80 in order to
    * compensate the pop that will occur in kernel's syscall_entry() just before
    * returning back to the user. That's because the nested_interrupts array is
    * global and not specific to any given task. Like the registers, it has to
    * be saved and restored in a consistent way.
    */

   if (!is_kernel_thread(ti)) {
      push_nested_interrupt(SYSCALL_SOFT_INTERRUPT);
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
   if (UNLIKELY(!(next->state_regs->eflags & EFLAGS_IF))) {

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
   bool should_drop_top_syscall = false;

   // XXX
   debug_switch_task_counter++;
   debug_switch_task_last_task = curr;
   // XXX

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

         arch_proc_members_t *arch = get_proc_arch_fields(ti->pi);
         set_curr_pdir(ti->pi->pdir);

         if (UNLIKELY(arch->ldt != NULL))
            load_ldt(arch->ldt_index_in_gdt, arch->ldt_size);
      }

      // XXX: disabled process signals during context switch
      //if (!ti->running_in_kernel)
      //   process_signals(ti, sig_in_usermode, state);

      if (is_fpu_enabled_for_task(ti)) {
         hw_fpu_enable();
         restore_fpu_regs(ti, false);
         /* leave FPU enabled */
      }
   }

   if (KRN_TRACK_NESTED_INTERR) {
      if (curr->running_in_kernel)
         if (!is_kernel_thread(curr))
            should_drop_top_syscall = true;
   }

   if (UNLIKELY(curr->state == TASK_STATE_ZOMBIE)) {
      ulong var;
      disable_interrupts(&var);
      {
         set_curr_task(kernel_process);
      }
      enable_interrupts(&var);
      free_mem_for_zombie_task(curr);
   }

   /* From here until the end, we have to be as fast as possible */
   disable_interrupts_forced();

   if (KRN_TRACK_NESTED_INTERR) {
      if (should_drop_top_syscall)
         nested_interrupts_drop_top_syscall();
   }

   enable_preemption_nosched();
   ASSERT(is_preemption_enabled());

   if (!ti->running_in_kernel)
      task_info_reset_kernel_stack(ti);
   else
      adjust_nested_interrupts_for_task_in_kernel(ti);

   set_curr_task(ti);
   ti->timer_ready = false;
   set_kernel_stack((ulong)ti->state_regs);

   // XXX
   debug_switch_task_counter++;
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

      if (arch->aligned_fpu_regs) {

         /*
          * We already have an FPU regs buffer: just clear its contents and
          * keep it allocated.
          */
         bzero(arch->aligned_fpu_regs, arch->fpu_regs_size);

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
   aligned_kfree2(arch->aligned_fpu_regs, arch->fpu_regs_size);
   arch->aligned_fpu_regs = NULL;
   arch->fpu_regs_size = 0;
}

void
arch_specific_new_proc_setup(struct process *pi, struct process *parent)
{
   arch_proc_members_t *arch = get_proc_arch_fields(pi);

   if (!parent)
      return;      /* we're done */

   memcpy(&pi->pi_arch, &parent->pi_arch, sizeof(pi->pi_arch));

   if (arch->ldt)
      gdt_entry_inc_ref_count(arch->ldt_index_in_gdt);

   for (int i = 0; i < ARRAY_SIZE(arch->gdt_entries); i++)
      if (arch->gdt_entries[i])
         gdt_entry_inc_ref_count(arch->gdt_entries[i]);

   pi->set_child_tid = NULL;
}

void
arch_specific_free_proc(struct process *pi)
{
   arch_proc_members_t *arch = get_proc_arch_fields(pi);

   if (arch->ldt) {
      gdt_clear_entry(arch->ldt_index_in_gdt);
      arch->ldt = NULL;
   }

   for (int i = 0; i < ARRAY_SIZE(arch->gdt_entries); i++) {
      if (arch->gdt_entries[i]) {
         gdt_clear_entry(arch->gdt_entries[i]);
         arch->gdt_entries[i] = 0;
      }
   }
}

static void
handle_fatal_error(regs_t *r, int signum)
{
   send_signal(get_curr_tid(), signum, SIG_FL_PROCESS | SIG_FL_FAULT);
}

/* General protection fault handler */
void handle_gpf(regs_t *r)
{
   if (!get_curr_task() || is_kernel_thread(get_curr_task()))
      panic("General protection fault. Error: %p\n", r->err_code);

   handle_fatal_error(r, SIGSEGV);
}

/* Illegal instruction fault handler */
void handle_ill(regs_t *r)
{
   if (!get_curr_task() || is_kernel_thread(get_curr_task()))
      panic("Illegal instruction fault. Error: %p\n", r->err_code);

   handle_fatal_error(r, SIGILL);
}

/* Division by zero fault handler */
void handle_div0(regs_t *r)
{
   if (!get_curr_task() || is_kernel_thread(get_curr_task()))
      panic("Division by zero fault. Error: %p\n", r->err_code);

   handle_fatal_error(r, SIGFPE);
}

/* Coproc fault handler */
void handle_cpf(regs_t *r)
{
   if (!get_curr_task() || is_kernel_thread(get_curr_task()))
      panic("Co-processor (fpu) fault. Error: %p\n", r->err_code);

   handle_fatal_error(r, SIGFPE);
}
