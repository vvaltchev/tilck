
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

static int internal_copy_user_str(void *dest,
                                  const void *user_ptr,
                                  void *dest_end,
                                  size_t *written_ptr)
{
   const char *ptr = user_ptr;
   char *d = dest;

   do {

      if (d >= (char *)dest_end)
         return 1;

      if ((uptr)ptr >= KERNEL_BASE_VA)
         return -1;

      *d++ = *ptr;

   } while (*ptr++);

   *written_ptr = (d - (char *)dest);

   printk("dest: '%s', len: %u\n", dest, *written_ptr);
   return 0;
}

/*
 * Copy an user-space NUL-terminated string into 'dest'. Destination's buffer
 * size is 'max_size'. Returns:
 *     0  if everything is alright
 *    -1  if the read from the user pointer causes a page fault
 *     1  if max_size is not enough
 */
int copy_str_from_user(void *dest, const void *user_ptr, size_t max_size)
{
   int rc;
   size_t written;
   ASSERT(!is_preemption_enabled());

   enter_in_user_copy();
   {
      rc = internal_copy_user_str(dest,
                                  user_ptr,
                                  (char *)dest + max_size,
                                  &written);
   }
   exit_in_user_copy();
   return rc;
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
   if (((uptr)user_ptr + sizeof(void *)) > KERNEL_BASE_VA)
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
   if (((uptr)user_ptr + sizeof(void *)) > KERNEL_BASE_VA)
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
                             size_t max_size, size_t *written_ptr)
{
   int rc = 0;
   int argc;
   char **dest_arr = (char **)dest;
   char *dest_end = (char *)dest + max_size;
   char *after_ptrs_arr;
   size_t written = 0;

   enter_in_user_copy();

   for (argc = 0; ; argc++) {

      uptr pval = (uptr)(user_arr + argc);

      if ((pval + sizeof(void*)) > KERNEL_BASE_VA) {
         rc = -1;
         goto out;
      }

      char *pval_deref = *(char **)pval;

      if (!pval_deref)
         break;
   }

   after_ptrs_arr = (char *) &dest_arr[argc + 1];

   if (after_ptrs_arr > dest_end) {
      rc = 1;
      goto out;
   }

   dest_arr[argc + 1] = NULL;

   after_ptrs_arr += sizeof(char *);
   written += (argc + 1) * sizeof(char *);

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
