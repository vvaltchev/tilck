
#include <process.h>
#include <string_util.h>
#include <kmalloc.h>
#include <arch/i386/process_int.h>
#include <hal.h>


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

int kthread_create(kthread_func_ptr fun)
{
   regs r;
   memset(&r, 0, sizeof(r));

   r.gs = r.fs = r.es = r.ds = r.ss = 0x10;
   r.cs = 0x08;

   r.eip = (u32) fun;
   r.eflags = get_eflags() | (1 << 9);

   task_info *pi = kmalloc(sizeof(task_info));
   INIT_LIST_HEAD(&pi->list);
   pi->pdir = get_kernel_page_dir();
   pi->pid = ++current_max_pid;
   pi->state = TASK_STATE_RUNNABLE;

   pi->owning_process_pid = 0; /* The pid of the "kernel process" is 0 */
   pi->kernel_stack = (void *) kmalloc(KTHREAD_STACK_SIZE);

   memset(pi->kernel_stack, 0, KTHREAD_STACK_SIZE);

   r.useresp = ((uptr) pi->kernel_stack + KTHREAD_STACK_SIZE - 1);
   r.useresp &= POINTER_ALIGN_MASK;

   // Pushes the address of kthread_exit() into thread's stack in order to
   // it to be called after thread's function returns.

   *(void **)(r.useresp) = (void *) &kthread_exit;
   r.useresp -= sizeof(void *);

   memmove(&pi->state_regs, &r, sizeof(r));

   add_task(pi);
   pi->ticks = 0;
   pi->total_ticks = 0;

   return pi->pid;
}

void kthread_exit()
{
   disable_preemption();

   task_info *ti = get_current_task();
   printk("****** [kernel thread] EXIT (pid: %i)\n", ti->pid);

   ti->state = TASK_STATE_ZOMBIE;

   // HACK: push a fake interrupt to compensate the call to
   // end_current_interrupt_handling() in switch_to_process(), done by the
   // scheduler.

   push_nested_interrupt(-1);

   asmVolatile("movl %0, %%esp" : : "i"(KERNEL_BASE_STACK_ADDR));
   asmVolatile("jmp *%0" : : "r"(&schedule));
}

NORETURN void first_usermode_switch(page_directory_t *pdir,
                                    void *entry,
                                    void *stack_addr)
{
   regs r;
   memset(&r, 0, sizeof(r));

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

   task_info *pi = kmalloc(sizeof(task_info));
   INIT_LIST_HEAD(&pi->list);

   pi->pdir = pdir;
   pi->pid = ++current_max_pid;
   pi->state = TASK_STATE_RUNNABLE;

   pi->owning_process_pid = pi->pid;
   pi->kernel_stack = NULL;

   memmove(&pi->state_regs, &r, sizeof(r));

   add_task(pi);
   pi->ticks = 0;
   pi->total_ticks = 0;

   disable_preemption();
   switch_to_task(pi);
}
