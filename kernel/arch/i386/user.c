
#include <exos/user.h>
#include <exos/process.h>
#include <exos/errno.h>

volatile bool in_user_copy;

void handle_user_copy_fault(void)
{
   in_user_copy = false;
   task_change_state(current, TASK_STATE_RUNNABLE);
   set_current_task_in_user_mode();
   set_return_register(&current->state_regs, -EFAULT);
   pop_nested_interrupt(); // The page fault
   enable_preemption();
   switch_to_idle_task();
}

int copy_from_user(void *dest, const void *user_ptr, size_t n)
{
   ASSERT(!is_preemption_enabled());

   uptr p = (uptr) user_ptr;

   if (p >= KERNEL_BASE_VA)
      return -1;

   in_user_copy = true;
   memcpy(dest, user_ptr, n);
   in_user_copy = false;
   return 0;
}

int copy_str_from_user(void *dest, const void *user_ptr)
{
   ASSERT(!is_preemption_enabled());
   in_user_copy = true;

   const char *ptr = user_ptr;
   char *d = dest;

   do {

      if ((uptr)ptr >= KERNEL_BASE_VA) {
         in_user_copy = false;
         return -1;
      }

      *d++ = *ptr++;

   } while (*ptr);

   in_user_copy = false;
   return 0;
}

int copy_to_user(void *user_ptr, const void *src, size_t n)
{
   ASSERT(!is_preemption_enabled());

   NOT_IMPLEMENTED();
}
