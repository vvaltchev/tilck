/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/string_util.h>
#include <tilck/common/unaligned.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/user.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/fault_resumable.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/paging.h>

#include <linux/auxvec.h> // system header

int copy_from_user(void *dest, const void *user_ptr, size_t n)
{
   if (user_out_of_range(user_ptr, n))
      return -1;

   u32 r = fault_resumable_call(PAGE_FAULT_MASK, memcpy, 3, dest, user_ptr, n);
   return !r ? 0 : -1;
}

int copy_to_user(void *user_ptr, const void *src, size_t n)
{
   if (user_out_of_range(user_ptr, n))
      return -1;

   u32 r = fault_resumable_call(PAGE_FAULT_MASK, memcpy, 3, user_ptr, src, n);
   return !r ? 0 : -1;
}

static void internal_copy_user_str(void *dest,
                                   const void *user_ptr,
                                   void *dest_end,
                                   size_t *written_ptr,
                                   int *rc)
{
   ASSERT(in_fault_resumable_code());

   const char *ptr = user_ptr;
   char *d = dest;
   *written_ptr = 0;
   *rc = 0;

   do {

      if (d >= (char *)dest_end) {
         *rc = 1;
         return;
      }

      if (user_out_of_range(ptr, 1)) {
         *rc = -1;
         return;
      }

      *d++ = *ptr; /* NOTE: `ptr` is NOT increased here */

   } while (*ptr++);

   *written_ptr = (size_t)(d - (char *)dest); /* NOTE: counting the final \0 */
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
   int rc;
   u32 faults;
   size_t written;

   faults = fault_resumable_call(PAGE_FAULT_MASK,
                                 internal_copy_user_str,
                                 5,
                                 dest,
                                 user_ptr,
                                 (char *)dest + max_size,
                                 &written,
                                 &rc);

   if (written_ptr)
      *written_ptr = written;

   if (faults)
      return -1;

   return rc;
}

static void
internal_copy_str_array_from_user(void *dest,
                                  const char *const *user_arr,
                                  size_t max_size,
                                  size_t *written_ptr,
                                  int *rc)
{
   int argc;
   char **dest_arr = (char **)dest;
   char *dest_end = (char *)dest + max_size;
   char *after_ptrs_arr;
   size_t written = 0;
   *rc = 0;

   ASSERT(in_fault_resumable_code());

   for (argc = 0; ; argc++) {

      const char *const *ptr_ptr = user_arr + argc;

      if (user_out_of_range(ptr_ptr, sizeof(void *))) {
         *rc = -1;
         goto out;
      }

      /*
       * OK, the double-pointer is in range, so we can de-reference it to
       * read the single (char *) pointer. We don't care at the moment if the
       * single pointer is valid, but we have to check whether it is NULL in
       * order to calculate 'argc'.
       */

      if (!*ptr_ptr)
         break;
   }

   if ((char *)&dest_arr[argc] > dest_end - sizeof(void *)) {
      *rc = 1;
      goto out;
   }

   /* this is safe, we've just checked that */
   WRITE_PTR(&dest_arr[argc], NULL);

   after_ptrs_arr = (char *) &dest_arr[argc + 1];
   written += (u32)(after_ptrs_arr - (char *)dest_arr);

   for (int i = 0; i < argc; i++) {

      size_t local_written = 0;
      WRITE_PTR(&dest_arr[i], after_ptrs_arr);

      internal_copy_user_str(after_ptrs_arr,
                             user_arr[i],
                             dest_end,
                             &local_written,
                             rc);

      if (*rc != 0)
         break;

      written += local_written;
      after_ptrs_arr += local_written;
   }

out:
   *written_ptr = written;
}


int copy_str_array_from_user(void *dest,
                             const char *const *user_arr,
                             size_t max_size,
                             size_t *written_ptr)
{
   int rc;
   u32 faults;

   faults = fault_resumable_call(PAGE_FAULT_MASK,
                                 internal_copy_str_array_from_user,
                                 5,
                                 dest,
                                 user_arr,
                                 max_size,
                                 written_ptr,
                                 &rc);

   if (faults)
      return -1;

   return rc;
}

int duplicate_user_path(char *dest,
                        const char *user_path,
                        size_t dest_size,
                        size_t *written_ptr /* IN/OUT */)
{
   int rc;
   size_t curr_written = 0;

   if (!user_path)
      return -EINVAL;

   if (*written_ptr >= dest_size)
      return -ENAMETOOLONG;

   rc = copy_str_from_user(dest + *written_ptr,
                           user_path,
                           dest_size - *written_ptr,
                           &curr_written);

   if (rc < 0)
      return -EFAULT;

   if (rc > 0)
      return -ENAMETOOLONG;

   *written_ptr += curr_written;
   return 0;
}

int duplicate_user_argv(char *dest,
                        const char *const *user_argv,
                        size_t dest_size,
                        size_t *written_ptr /* IN/OUT */)
{
   int rc;
   size_t curr_written = 0;

   if (*written_ptr >= dest_size)
      return -E2BIG;

   rc = copy_str_array_from_user(dest + *written_ptr,
                                 user_argv,
                                 dest_size - *written_ptr,
                                 &curr_written);

   if (rc < 0)
      return -EFAULT;

   if (rc > 0)
      return -E2BIG;

   *written_ptr += curr_written;
   return 0;
}

void push_on_stack(ulong **stack_ptr_ref, ulong val)
{
   (*stack_ptr_ref)--;     // Decrease the value of the stack pointer
   **stack_ptr_ref = val;  // *stack_ptr = val
}

void push_on_stack2(pdir_t *pdir, ulong **stack_ptr_ref, ulong val)
{
   // Decrease the value of the stack pointer
   (*stack_ptr_ref)--;

   // *stack_ptr = val
   debug_checked_virtual_write(pdir, *stack_ptr_ref, &val, sizeof(ulong));
}

void push_on_user_stack(regs_t *r, ulong val)
{
   ulong user_sp = regs_get_usersp(r);
   push_on_stack((ulong **)&user_sp, val);
   regs_set_usersp(r, user_sp);
}

void push_string_on_user_stack(regs_t *r, const char *str)
{
   const size_t len = strlen(str) + 1; // count also the '\0'
   const size_t aligned_len = round_down_at(len, USER_STACK_STRING_ALIGN);
   const size_t rem = len - aligned_len;

   ulong user_sp = regs_get_usersp(r);
   user_sp -= aligned_len + (rem > 0 ? USER_STACK_STRING_ALIGN : 0);
   regs_set_usersp(r, user_sp);
   memcpy(TO_PTR(user_sp), str, aligned_len);

   if (rem > 0) {
      char smallbuf[USER_STACK_STRING_ALIGN] = {0};
      memcpy(&smallbuf, str + aligned_len, rem);
      memcpy(TO_PTR(user_sp + aligned_len), &smallbuf, sizeof(smallbuf));
   }
}

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
