/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/string_util.h>
#include <tilck/common/unaligned.h>
#include <tilck/common/utils.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/user.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/fault_resumable.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/signal.h>

#include <linux/auxvec.h> // system header

/* User-copy fixup landing pad, defined in kernel/arch/<arch>/user_copy.S */
void asm_user_copy_fault(void);

int copy_from_user(void *dest, const void *user_ptr, size_t n)
{
   struct task *curr = get_curr_task();
   int rc;

   if (user_out_of_range(user_ptr, n))
      return -1;

   ASSERT(!curr->user_access_fixup);    /* user copies never nest */
   curr->user_access_fixup = asm_user_copy_fault;
   rc = arch_user_copy(dest, user_ptr, n);
   curr->user_access_fixup = NULL;

   return rc ? -1 : 0;
}

int copy_to_user(void *user_ptr, const void *src, size_t n)
{
   struct task *curr = get_curr_task();
   int rc;

   if (user_out_of_range(user_ptr, n))
      return -1;

   ASSERT(!curr->user_access_fixup);    /* user copies never nest */
   curr->user_access_fixup = asm_user_copy_fault;
   rc = arch_user_copy(user_ptr, src, n);
   curr->user_access_fixup = NULL;

   if (!rc)
      return 0;

   /*
    * A page fault was caught mid-copy. user_access_resume_on_fault() set
    * fault_resume_reason = -ENOMEM for an out-of-memory CoW page; for an
    * ordinary bad-pointer fault the reason is left 0, so we report -1 as usual.
    */
   return curr->fault_resume_reason ? curr->fault_resume_reason : -1;
}

bool user_access_resume_on_fault(regs_t *r, bool out_of_mem)
{
   struct task *curr = get_curr_task();

   if (!curr->user_access_fixup)
      return false;

   curr->fault_resume_reason = out_of_mem ? -ENOMEM : 0;
   regs_set_ip(r, (ulong)curr->user_access_fixup);
   return true;
}

void handle_cow_out_of_mem(regs_t *r)
{
   struct task *curr = get_curr_task();

   if (is_fault_resumable(regs_intnum(r))) {

      /*
       * The kernel hit OOM while servicing a CoW page during a fault-resumable
       * primitive (copy_to_user() & friends). Unwind to the caller with -ENOMEM
       * instead of crashing; the faulting page is left untouched (still CoW).
       */
      curr->fault_resume_reason = -ENOMEM;
      handle_resumable_fault(r);    /* NORETURN: unwinds into the primitive */
      NOT_REACHED();
   }

   if (!in_syscall(curr)) {

      /* User-mode CoW fault under memory pressure: kill the process. */
      printk("Out-of-memory: killing pid %d\n", get_curr_pid());
      send_signal(get_curr_pid(), SIGKILL, SIG_FL_PROCESS | SIG_FL_FAULT);
      return;
   }

   /* In-kernel CoW fault outside a resumable primitive == a real bug. */
   panic("Out-of-memory: can't service a CoW page [pid %d]", get_curr_pid());
}

/*
 * Per-chunk cap for internal_copy_user_str(): arch_user_copy() reads a whole
 * chunk before we scan it for the NUL, so we bound the chunk to keep the
 * over-read past the NUL cheap (it never crosses a page, so it can only touch
 * already-mapped user bytes). 64 reads a typical short path/arg in one shot
 * while keeping the read well under a page.
 */
#define USER_STR_COPY_CHUNK 64u

/*
 * Copy a NUL-terminated user string into 'dest' (capacity dest_end - dest)
 * using arch_user_copy(); the caller must have armed user_access_fixup. The
 * read walks the user buffer in page-bounded chunks, so a fault means the
 * current page is genuinely unmapped -- exactly when a byte-at-a-time loop
 * would fault -- and never an over-read past the NUL into a later, unmapped
 * page. That also reduces the user-range check to one bound per chunk rather
 * than one per byte. Sets *written_ptr (length including the NUL) on success.
 * Returns 0 on success, 1 if 'dest' is too small, -1 on a faulting read.
 */
static int
internal_copy_user_str(char *dest,
                       const char *user_ptr,
                       char *dest_end,
                       size_t *written_ptr)
{
   char *d = dest;
   const char *p = user_ptr;

   ASSERT(get_curr_task()->user_access_fixup);

   *written_ptr = 0;

   while (d < dest_end) {

      const ulong pv = (ulong)p;
      size_t i;

      if (pv >= BASE_VA)
         return -1;                        /* walked off the user space */

      /* Bytes still free in the destination buffer. */
      const size_t dest_room = (size_t)(dest_end - d);

      /*
       * Bytes from `p` to the end of its page. A read must not cross into the
       * next page, which can be unmapped even when this one is fine -- that is
       * what keeps the over-read past the NUL safe.
       */
      const size_t page_room = PAGE_SIZE - (pv & (PAGE_SIZE - 1));

      /* Bytes from `p` to the top of user space, so we never read past
       * BASE_VA into kernel memory. */
      const size_t user_room = (size_t)(BASE_VA - pv);

      /* The most we may read in one go: the smallest of the three bounds. */
      const size_t avail = MIN3(dest_room, page_room, user_room);

      /* Cap the read so the over-read past the NUL stays cheap. */
      const size_t chunk = MIN(avail, USER_STR_COPY_CHUNK);

      if (arch_user_copy(d, p, chunk))
         return -1;                        /* unmapped user page */

      /* Scan the freshly-copied chunk for the string's NUL terminator. */
      for (i = 0; i < chunk && d[i]; i++) { }

      if (i < chunk) {                              /* found the NUL */
         *written_ptr = (size_t)(d - dest) + i + 1; /* count the final \0 */
         return 0;
      }

      d += chunk;
      p += chunk;
   }

   return 1;                               /* 'dest' too small, no NUL found */
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
   struct task *curr = get_curr_task();
   char *const dest_end = (char *)dest + max_size;
   size_t written = 0;
   int rc;

   ASSERT(!curr->user_access_fixup);    /* user copies never nest */
   curr->user_access_fixup = asm_user_copy_fault;
   rc = internal_copy_user_str(dest, user_ptr, dest_end, &written);
   curr->user_access_fixup = NULL;

   if (written_ptr)
      *written_ptr = written;

   return rc;
}

static int
internal_copy_str_array_from_user(void *dest,
                                  const char *const *user_arr,
                                  size_t max_size,
                                  size_t *written_ptr)
{
   char **dest_arr = (char **)dest;
   char *dest_end = (char *)dest + max_size;
   char *after_ptrs_arr;
   size_t written = 0;
   int rc = 0;
   int argc;

   ASSERT(get_curr_task()->user_access_fixup);

   for (argc = 0; ; argc++) {

      const void *uptr;

      if (user_out_of_range(user_arr + argc, sizeof(uptr))) {
         rc = -1;
         goto out;
      }

      /*
       * The double-pointer is in range; read the single (char *) it points
       * to. We don't validate that pointer yet -- we just need to know whether
       * it is NULL, in order to compute 'argc'.
       */

      if (arch_user_copy(&uptr, user_arr + argc, sizeof(uptr))) {
         rc = -1;
         goto out;
      }

      if (!uptr)
         break;
   }

   if ((char *)&dest_arr[argc] > dest_end - sizeof(void *)) {
      rc = 1;
      goto out;
   }

   /* this is safe, we've just checked that */
   WRITE_PTR(&dest_arr[argc], NULL);

   after_ptrs_arr = (char *)&dest_arr[argc + 1];
   written += (size_t)(after_ptrs_arr - (char *)dest_arr);

   for (int i = 0; i < argc; i++) {

      const void *uptr;
      size_t local_written = 0;

      WRITE_PTR(&dest_arr[i], after_ptrs_arr);

      /* user_arr + i was already range-checked by the loop above */
      if (arch_user_copy(&uptr, user_arr + i, sizeof(uptr))) {
         rc = -1;
         break;
      }

      rc = internal_copy_user_str(after_ptrs_arr,
                                  uptr,
                                  dest_end,
                                  &local_written);

      if (rc != 0)
         break;

      written += local_written;
      after_ptrs_arr += local_written;
   }

out:
   *written_ptr = written;
   return rc;
}


int copy_str_array_from_user(void *dest,
                             const char *const *user_arr,
                             size_t max_size,
                             size_t *written_ptr)
{
   struct task *curr = get_curr_task();
   int rc;

   ASSERT(!curr->user_access_fixup);    /* user copies never nest */
   curr->user_access_fixup = asm_user_copy_fault;
   rc = internal_copy_str_array_from_user(dest,
                                          user_arr,
                                          max_size,
                                          written_ptr);
   curr->user_access_fixup = NULL;

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
   const size_t aligned_len = round_down_at(len, USERMODE_STACK_ALIGN);
   const size_t rem = len - aligned_len;

   ulong user_sp = regs_get_usersp(r);

   user_sp -= aligned_len + (rem > 0 ? USERMODE_STACK_ALIGN : 0);
   regs_set_usersp(r, user_sp);
   memcpy(TO_PTR(user_sp), str, aligned_len);

   if (rem > 0) {
      char smallbuf[USERMODE_STACK_ALIGN] = {0};
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
