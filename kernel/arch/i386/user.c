
#include <exos/user.h>
#include <exos/process.h>
#include <exos/errno.h>

static volatile bool in_user_copy;

bool is_in_user_copy(void)
{
   return in_user_copy;
}

static inline void enter_in_user_copy(void)
{
   in_user_copy = true;
}

static inline void exit_in_user_copy(void)
{
   in_user_copy = false;
}

void handle_user_copy_fault(void)
{
   exit_in_user_copy();
   task_change_state(get_curr_task(), TASK_STATE_RUNNABLE);
   set_current_task_in_user_mode();
   set_return_register(&get_curr_task()->state_regs, -EFAULT);
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

   enter_in_user_copy();
   {
      memcpy(dest, user_ptr, n);
   }
   exit_in_user_copy();
   return 0;
}

int copy_str_from_user(void *dest, const void *user_ptr)
{
   ASSERT(!is_preemption_enabled());

   enter_in_user_copy();
   {
      const char *ptr = user_ptr;
      char *d = dest;

      do {

         if ((uptr)ptr >= KERNEL_BASE_VA) {
            in_user_copy = false;
            return -1;
         }

         *d++ = *ptr++;

      } while (*ptr);

   }
   exit_in_user_copy();
   return 0;
}

int copy_to_user(void *user_ptr, const void *src, size_t n)
{
   ASSERT(!is_preemption_enabled());

   if (((uptr)user_ptr + n) >= KERNEL_BASE_VA)
      return -1;

   enter_in_user_copy();
   {
      memcpy(user_ptr, src, n);
   }
   exit_in_user_copy();
   return 0;
}

int check_user_ptr_size_writable(void *user_ptr)
{
   if (((uptr)user_ptr + sizeof(void *)) >= KERNEL_BASE_VA)
      return -1;

   enter_in_user_copy();
   {
      uptr saved_val;
      /* Just read and write the user_ptr to check that it is writable */
      memcpy(&saved_val, user_ptr, sizeof(void *));
      memcpy(user_ptr, &saved_val, sizeof(void *));
   }
   exit_in_user_copy();
   return 0;
}

int check_user_ptr_size_readable(void *user_ptr)
{
   if (((uptr)user_ptr + sizeof(void *)) >= KERNEL_BASE_VA)
      return -1;

   enter_in_user_copy();
   {
      uptr saved_val;
      /* Just read the user_ptr to check that we won't get a page fault */
      memcpy(&saved_val, user_ptr, sizeof(void *));
   }
   exit_in_user_copy();
   return 0;
}
