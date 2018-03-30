
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
   regs r;
   bzero(&r, sizeof(r));

   r.gs = r.fs = r.es = r.ds = r.ss = 0x10;
   r.cs = 0x08;

   r.eip = (u32) fun;
   r.eflags = 0x2 /* reserved, should be always set */ | EFLAGS_IF;

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

   r.eflags = 0x2 /* reserved, always set */ | EFLAGS_IF;

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
   task_info_reset_kernel_stack(ti);
   return ti;
}

void save_current_task_state(regs *r)
{
   ASSERT(current != NULL);

   if (current->running_in_kernel) {

      current->kernel_state_regs = r;
      DEBUG_VALIDATE_STACK_PTR();

   } else {
      memcpy(&current->state_regs, r, sizeof(*r));
   }
}

static task_info fake_current_proccess;

void panic_save_current_task_state(regs *r)
{
   if (UNLIKELY(current == NULL)) {

      /*
       * PANIC occurred before the first task is started.
       * Create a fake current task just to allow the rest of the panic code
       * to not handle the current == NULL case.
       */

      fake_current_proccess.pid = -1;
      fake_current_proccess.running_in_kernel = true;
      current = &fake_current_proccess;
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

   memcpy(&current->state_regs, r, sizeof(*r));
   current->kernel_state_regs = &current->state_regs;
}

/*
 * Sched functions that are here beacuse of arch-specific statements.
 */

void set_current_task_in_user_mode(void)
{
   ASSERT(!is_preemption_enabled());
   current->running_in_kernel = 0;

   task_info_reset_kernel_stack(current);
   set_kernel_stack((u32)current->kernel_state_regs);
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
   pop_nested_interrupt();

   if (current && current->running_in_kernel && !is_kernel_thread(current)) {
      nested_interrupts_drop_top_syscall();
   }

   enable_preemption();
   ASSERT(is_preemption_enabled());

   DEBUG_VALIDATE_STACK_PTR();

   // We have to be SURE that the timer IRQ is NOT masked!
   irq_clear_mask(X86_PC_TIMER_IRQ);

   current = ti; /* this is safe here: the interrupts are disabled! */

   if (!ti->running_in_kernel) {

      task_info_reset_kernel_stack(ti);
      set_kernel_stack((u32) ti->kernel_state_regs);
      ASSERT(ti->state_regs.eflags & EFLAGS_IF);

      context_switch(&ti->state_regs);

   } else {

      if (!is_kernel_thread(ti)) {
         push_nested_interrupt(SYSCALL_SOFT_INTERRUPT);
      }

      set_kernel_stack((u32) ti->kernel_state_regs);
      kernel_context_switch(ti->kernel_state_regs);
   }
}
