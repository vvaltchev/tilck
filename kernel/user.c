
#include <exos/user.h>
#include <exos/process.h>
#include <exos/errno.h>

static volatile bool __in_user_copy;

bool in_user_copy(void)
{
   return __in_user_copy;
}

static inline void enter_in_user_copy(void)
{
   __in_user_copy = true;
}

static inline void exit_in_user_copy(void)
{
   __in_user_copy = false;
}

void handle_user_copy_fault(void)
{
   exit_in_user_copy();
   task_change_state(get_curr_task(), TASK_STATE_RUNNABLE);

   set_current_task_in_user_mode();

   /*
    * Move back state_regs 1 struct regs up, since we have to preserve the
    * user regs, for switch_to_task(). This is special case: usually
    * set_current_task_in_user_mode() is called at the end of handle_syscall(),
    * where completely resetting the state_regs pointer is what we want (we
    * already keep that in ESP).
    */
   get_curr_task()->state_regs--;
   set_return_register(get_curr_task()->state_regs, -EFAULT);

   pop_nested_interrupt(); // The page fault
   enable_preemption();
   switch_to_idle_task();
}

int copy_from_user(void *dest, const void *user_ptr, size_t n)
{
   ASSERT(!is_preemption_enabled());

   if (user_out_of_range(user_ptr, n))
      return -1;

   enter_in_user_copy();
   {
      memcpy(dest, user_ptr, n);
   }
   exit_in_user_copy();
   return 0;
}

static int internal_copy_user_str(void *dest,
                                  const void *user_ptr,
                                  void *dest_end,
                                  size_t *written_ptr)
{
   ASSERT(!is_preemption_enabled());
   ASSERT(in_user_copy());

   const char *ptr = user_ptr;
   char *d = dest;
   *written_ptr = 0;

   do {

      if (d >= (char *)dest_end)
         return 1;

      if (user_out_of_range(ptr, 1))
         return -1;

      *d++ = *ptr; /* note: ptr is NOT increased here */

   } while (*ptr++);

   *written_ptr = (d - (char *)dest); /* written includes the final \0 */
   return 0;
}

/*
 * Copy an user-space NUL-terminated string into 'dest'. Destination's buffer
 * size is 'max_size'. Returns:
 *     0  if everything is alright
 *    -1  if the read from the user pointer causes a page fault
 *     1  if max_size is not enough
 */
int copy_str_from_user(void *dest,
                       const void *user_ptr,
                       size_t max_size,
                       size_t *written_ptr)
{
   ASSERT(!is_preemption_enabled());

   int rc;
   size_t written;

   enter_in_user_copy();
   {
      rc = internal_copy_user_str(dest,
                                  user_ptr,
                                  (char *)dest + max_size,
                                  &written);

      if (written_ptr)
         *written_ptr = written;
   }
   exit_in_user_copy();
   return rc;
}

int copy_to_user(void *user_ptr, const void *src, size_t n)
{
   ASSERT(!is_preemption_enabled());

   if (user_out_of_range(user_ptr, n))
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
   if (user_out_of_range(user_ptr, sizeof(void *)))
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
   if (user_out_of_range(user_ptr, sizeof(void *)))
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

int copy_str_array_from_user(void *dest,
                             const char *const *user_arr,
                             size_t max_size,
                             size_t *written_ptr)
{
   int rc = 0;
   int argc;
   char **dest_arr = (char **)dest;
   char *dest_end = (char *)dest + max_size;
   char *after_ptrs_arr;
   size_t written = 0;

   enter_in_user_copy();

   for (argc = 0; ; argc++) {

      const char *const *ptr_ptr = user_arr + argc;

      if (user_out_of_range(ptr_ptr, sizeof(void *))) {
         rc = -1;
         goto out;
      }

      /*
       * OK, the double-pointer is in range, so we can de-reference it to
       * read the single (char *) pointer. We don't care at the moment if the
       * single pointer is valid, but we can whenever it is NULL in order to
       * calculate 'argc'.
       */

      if (!*ptr_ptr)
         break;
   }

   if ((char *)&dest_arr[argc + 1] > dest_end) {
      rc = 1;
      goto out;
   }

   dest_arr[argc + 1] = NULL;
   after_ptrs_arr = (char *) &dest_arr[argc + 2];
   written += (after_ptrs_arr - (char *)dest_arr);

   for (int i = 0; i < argc; i++) {

      size_t local_written = 0;
      dest_arr[i] = after_ptrs_arr;

      rc = internal_copy_user_str(after_ptrs_arr,
                                  user_arr[i],
                                  dest_end,
                                  &local_written);

      written += local_written;
      after_ptrs_arr += local_written;

      if (rc != 0)
         break;
   }

out:
   *written_ptr = written;
   exit_in_user_copy();
   return rc;
}


int duplicate_user_path(char *dest,
                        const char *user_path,
                        size_t dest_size,
                        size_t *written_ptr /* IN/OUT */)
{
   int rc;

   if (!user_path)
      return -EINVAL;

   rc = copy_str_from_user(dest + *written_ptr,
                           user_path,
                           dest_size - *written_ptr,
                           written_ptr);

   if (rc < 0)
      return -EFAULT;

   if (rc > 0)
      return -ENAMETOOLONG;

   return 0;
}

int duplicate_user_argv(char *dest,
                        const char *const *user_argv,
                        size_t dest_size,
                        size_t *written_ptr /* IN/OUT */)
{
   int rc;

   rc = copy_str_array_from_user(dest + *written_ptr,
                                 user_argv,
                                 dest_size - *written_ptr,
                                 written_ptr);

   if (rc < 0)
      return -EFAULT;

   if (rc > 0)
      return -E2BIG;

   return 0;
}
