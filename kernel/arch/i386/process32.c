
#include <common/string_util.h>

#include <exos/process.h>
#include <exos/kmalloc.h>
#include <exos/tasklet.h>

//#define DEBUG_printk printk
#define DEBUG_printk(...)

void task_info_reset_kernel_stack(task_info *ti)
{
   ti->kernel_state_regs.useresp = \
      ((uptr) ti->kernel_stack +
      KTHREAD_STACK_SIZE - 1) & POINTER_ALIGN_MASK;
}

void push_on_user_stack(regs *r, uptr val)
{
   r->useresp -= sizeof(val);
   memcpy((void *)r->useresp, &val, sizeof(val));
}

void push_string_on_user_stack(regs *r, const char *str)
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
   regs r;
   bzero(&r, sizeof(r));

   r.gs = r.fs = r.es = r.ds = r.ss = 0x10;
   r.cs = 0x08;

   r.eip = (u32) fun;
   r.eflags = get_eflags() | (1 << 9);

   task_info *ti = kzmalloc(sizeof(task_info));

   list_node_init(&ti->list);
   ti->pdir = get_kernel_page_dir();
   ti->pid = ++current_max_pid;
   ti->state = TASK_STATE_RUNNABLE;

   ti->owning_process_pid = 0; /* The pid of the "kernel process" is 0 */
   ti->running_in_kernel = 1;
   ti->kernel_stack = kzmalloc(KTHREAD_STACK_SIZE);
   VERIFY(ti->kernel_stack != NULL);

   bzero(&ti->state_regs, sizeof(r));
   memcpy(&ti->kernel_state_regs, &r, sizeof(r));

   task_info_reset_kernel_stack(ti);

   push_on_user_stack(&ti->kernel_state_regs, (uptr) arg);

   /*
    * Pushes the address of kthread_exit() into thread's stack in order to
    * it to be called after thread's function returns.
    * This is AS IF kthread_exit() called the thread 'fun' with a CALL
    * instruction before doing anything else. That allows the RET by 'fun' to
    * jump in the begging of kthread_exit().
    */

   push_on_user_stack(&ti->kernel_state_regs, (uptr) &kthread_exit);
   ti->kernel_state_regs.useresp -= sizeof(uptr);

   add_task(ti);
   return ti;
}

void remove_dead_kthread_tasklet(task_info *task)
{
   ASSERT(task->state == TASK_STATE_ZOMBIE);

   while (true) {

      disable_preemption();

      if (current != task) {
         printk("[kernel] remove_dead_kthread_tasklet (pid: %i)\n", task->pid);
         remove_task(task);
         enable_preemption();
         break;
      }

      enable_preemption();
      kernel_yield();
   }
}

void kthread_exit(void)
{
   disable_preemption();

   task_info *ti = get_current_task();
   printk("****** [kernel thread] EXIT (pid: %i)\n", ti->pid);

   task_change_state(ti, TASK_STATE_ZOMBIE);

   enqueue_tasklet1(&remove_dead_kthread_tasklet, ti);
   schedule_outside_interrupt_context();
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
   regs r;

   bzero(&r, sizeof(r));

   // User data selector with bottom 2 bits set for ring 3.
   r.gs = r.fs = r.es = r.ds = r.ss = 0x23;

   // User code selector with bottom 2 bits set for ring 3.
   r.cs = 0x1b;

   r.eip = (u32) entry;
   r.useresp = (u32) stack_addr;

   while (argv[argv_elems]) argv_elems++;
   while (env[env_elems]) env_elems++;
   push_args_on_user_stack(&r, argv, argv_elems, env, env_elems);

   r.eflags = get_eflags() | EFLAGS_IF;

   if (!task_to_use) {
      ti = kzmalloc(sizeof(task_info));
      list_node_init(&ti->list);
      ti->pid = ++current_max_pid;
      add_task(ti);
      ti->state = TASK_STATE_RUNNABLE;
      memcpy(ti->cwd, "/", 2);
   } else {
      ti = task_to_use;
      ASSERT(ti->state == TASK_STATE_RUNNABLE);
   }

   ti->pdir = pdir;

   ti->owning_process_pid = ti->pid;
   ti->running_in_kernel = false;

   if (!task_to_use) {
      ti->kernel_stack = kzmalloc(KTHREAD_STACK_SIZE);
   }

   memcpy(&ti->state_regs, &r, sizeof(r));
   bzero(&ti->kernel_state_regs, sizeof(r));

   task_info_reset_kernel_stack(ti);
   return ti;
}


task_info fake_current_proccess;

void save_current_task_state(regs *r)
{
   if (!current) {
      /*
       * PANIC occurred before the first task is started.
       * Create a fake current task just to store the registers.
       */

      fake_current_proccess.pid = -1;
      fake_current_proccess.running_in_kernel = true;
      current = &fake_current_proccess;
   }

   regs *state = current->running_in_kernel
                    ? &current->kernel_state_regs
                    : &current->state_regs;

   memcpy(state, r, sizeof(*r));

   if (current->running_in_kernel) {

      /*
       * If the current task was running in kernel, than the useresp has not
       * be saved on the stack by the CPU, since there has been not priviledge
       * change. So, we have to use the actual value of ESP as 'useresp' and
       * adjust it by +16. That's because when the interrupt occured, the CPU
       * pushed on the stack CS+EIP and we pushed int_num + err_code; in total,
       * 4 pointer-size integers.
       */
      state->useresp = r->esp + 16;

      state->eflags = get_eflags();
      state->ss = 0x10;

      if (!is_kernel_thread(current)) {
         DEBUG_printk("[kernel] PREEMPTING kernel code for user program!\n");
         DEBUG_VALIDATE_STACK_PTR();
      }
   }
}

/*
 * Sched functions that are here beacuse of arch-specific statements.
 */

void set_current_task_in_user_mode(void)
{
   ASSERT(!is_preemption_enabled());
   current->running_in_kernel = 0;

   task_info_reset_kernel_stack(current);
   set_kernel_stack(current->kernel_state_regs.useresp);
}


NORETURN void switch_to_task(task_info *ti)
{
   ASSERT(!current || current->state != TASK_STATE_RUNNING);
   ASSERT(ti->state == TASK_STATE_RUNNABLE);
   ASSERT(ti != current);

   DEBUG_printk("[sched] Switching to pid: %i %s %s\n",
                ti->pid,
                is_kernel_thread(ti) ? "[KTHREAD]" : "[USER]",
                ti->running_in_kernel ? "(kernel mode)" : "(usermode)");

   task_change_state(ti, TASK_STATE_RUNNING);
   ti->ticks = 0;

   if (get_curr_page_dir() != ti->pdir) {
      set_page_directory(ti->pdir);
   }

   disable_interrupts_forced();

   ASSERT(disable_interrupts_count == 1);
   ASSERT(!are_interrupts_enabled());

   pop_nested_interrupt();

   if (current &&
       current->running_in_kernel && !is_kernel_thread(current)) {

      if (nested_interrupts_count > 0) {

         ASSERT(nested_interrupts_count == 1);
         ASSERT(nested_interrupts[0] == 0x80); // int 0x80 (syscall)
         pop_nested_interrupt();
      }
   }

   enable_preemption();
   ASSERT(is_preemption_enabled());

   regs *state = ti->running_in_kernel
                    ? &ti->kernel_state_regs
                    : &ti->state_regs;

   ASSERT(!are_interrupts_enabled());

   /*
    * The interrupts will be enabled after the context switch even if they are
    * disabled now, so only in this special context is OK to make that counter
    * equal to 0, without enabling the interrupts.
    */
   disable_interrupts_count = 0;

   DEBUG_VALIDATE_STACK_PTR();

   // We have to be SURE that the timer IRQ is NOT masked!
   irq_clear_mask(X86_PC_TIMER_IRQ);

   if (!ti->running_in_kernel) {

      bzero(ti->kernel_stack, KTHREAD_STACK_SIZE);

      task_info_reset_kernel_stack(ti);
      set_kernel_stack(ti->kernel_state_regs.useresp);

      /*
       * ASSERT that the 9th bit in task's eflags is 1, which means that on
       * IRET the CPU will enable the interrupts.
       */

      ASSERT(state->eflags & EFLAGS_IF);

      current = ti;
      context_switch(state);

   } else {

      if (!is_kernel_thread(ti)) {
         push_nested_interrupt(0x80);
      }

      /*
       * Forcibily disable interrupts: that because we don't want them to be
       * automatically enabled when asm_kernel_context_switch_x86 runs POPF.
       * And that's because asm_kernel_context_switch_x86 has to POP ESP before
       * finally enabling them. See the comment in asm_kernel_context_switch_x86
       * for more about this.
       */
      state->eflags &= ~EFLAGS_IF;

      set_kernel_stack(ti->kernel_state_regs.useresp);
      current = ti;
      kernel_context_switch(state);
   }
}
