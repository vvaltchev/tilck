
#include <process.h>
#include <string_util.h>
#include <kmalloc.h>
#include <arch/i386/process_int.h>
#include <hal.h>

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

void push_args_on_user_stack(regs *r, int argc,
                             char **argv, int envc, char **env)
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

task_info *kthread_create(kthread_func_ptr fun)
{
   regs r;
   bzero(&r, sizeof(r));

   r.gs = r.fs = r.es = r.ds = r.ss = 0x10;
   r.cs = 0x08;

   r.eip = (u32) fun;
   r.eflags = get_eflags() | (1 << 9);

   task_info *ti = kzmalloc(sizeof(task_info));

   INIT_LIST_HEAD(&ti->list);
   ti->pdir = get_kernel_page_dir();
   ti->pid = ++current_max_pid;
   ti->state = TASK_STATE_RUNNABLE;

   ti->owning_process_pid = 0; /* The pid of the "kernel process" is 0 */
   ti->running_in_kernel = 1;
   ti->kernel_stack = kmalloc(KTHREAD_STACK_SIZE);
   bzero(ti->kernel_stack, KTHREAD_STACK_SIZE);

   bzero(&ti->state_regs, sizeof(r));
   memmove(&ti->kernel_state_regs, &r, sizeof(r));

   task_info_reset_kernel_stack(ti);

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

void kthread_exit()
{
   disable_preemption();

   task_info *ti = get_current_task();
   printk("****** [kernel thread] EXIT (pid: %i)\n", ti->pid);

   task_change_state(ti, TASK_STATE_ZOMBIE);

   asmVolatile("movl %0, %%esp" : : "i"(KERNEL_BASE_STACK_ADDR));
   asmVolatile("jmp *%0" : : "r"(&schedule_outside_interrupt_context));
}

task_info *create_first_usermode_task(page_directory_t *pdir,
                                      void *entry,
                                      void *stack_addr)
{
   regs r;
   bzero(&r, sizeof(r));

   // User data selector with bottom 2 bits set for ring 3.
   r.gs = r.fs = r.es = r.ds = r.ss = 0x23;

   // User code selector with bottom 2 bits set for ring 3.
   r.cs = 0x1b;

   r.eip = (u32) entry;
   r.useresp = (u32) stack_addr;

   char *argv[] = { "init", "test_arg_1" };
   char *env[] = { "OSTYPE=gnu-linux", "PWD=/" };
   push_args_on_user_stack(&r, ARRAY_SIZE(argv), argv, ARRAY_SIZE(env), env);

   r.eflags = get_eflags() | (1 << 9);

   task_info *ti = kzmalloc(sizeof(task_info));
   INIT_LIST_HEAD(&ti->list);

   ti->pdir = pdir;
   ti->pid = ++current_max_pid;
   ti->state = TASK_STATE_RUNNABLE;

   ti->owning_process_pid = ti->pid;
   ti->running_in_kernel = 0;
   ti->kernel_stack = kmalloc(KTHREAD_STACK_SIZE);
   bzero(ti->kernel_stack, KTHREAD_STACK_SIZE);

   memmove(&ti->state_regs, &r, sizeof(r));
   bzero(&ti->kernel_state_regs, sizeof(r));

   task_info_reset_kernel_stack(ti);
   add_task(ti);
   return ti;
}
