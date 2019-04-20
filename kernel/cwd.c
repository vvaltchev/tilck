/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/process.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/syscalls.h>

int sys_chdir(const char *user_path)
{
   int rc = 0;
   struct stat64 statbuf;
   task_info *curr = get_curr_task();
   process_info *pi = curr->pi;
   char *orig_path = curr->args_copybuf;
   char *path = curr->args_copybuf + ARGS_COPYBUF_SIZE / 2;

   STATIC_ASSERT(ARRAY_SIZE(pi->cwd) == MAX_PATH);
   STATIC_ASSERT((ARGS_COPYBUF_SIZE / 2) >= MAX_PATH);

   rc = copy_str_from_user(orig_path, user_path, MAX_PATH, NULL);

   if (rc < 0)
      return -EFAULT;

   if (rc > 0)
      return -ENAMETOOLONG;

   kmutex_lock(&pi->fslock);
   {
      if ((rc = compute_abs_path(orig_path, pi->cwd, path, MAX_PATH)))
         goto out;

      if ((rc = vfs_stat64(path, &statbuf)))
         goto out;

      if (!S_ISDIR(statbuf.st_mode)) {
         rc = -ENOTDIR;
         goto out;
      }

      size_t pl = strlen(path);
      memcpy(pi->cwd, path, pl + 1);

      if (pl > 1) {

         /* compute_abs_path always returns a path without a trailing '/' */
         ASSERT(pi->cwd[pl - 1] != '/');

         /* on the other side, pi->cwd has always a trailing '/' */
         pi->cwd[pl] = '/';
         pi->cwd[pl + 1] = 0;
      }
   }

out:
   kmutex_unlock(&pi->fslock);
   return rc;
}

int sys_getcwd(char *user_buf, size_t buf_size)
{
   int ret;
   size_t cwd_len;
   disable_preemption();
   {
      cwd_len = strlen(get_curr_task()->pi->cwd) + 1;

      if (!user_buf || !buf_size) {
         ret = -EINVAL;
         goto out;
      }

      if (buf_size < cwd_len) {
         ret = -ERANGE;
         goto out;
      }

      ret = copy_to_user(user_buf, get_curr_task()->pi->cwd, cwd_len);

      if (ret < 0) {
         ret = -EFAULT;
         goto out;
      }

      if (cwd_len > 2) { /* NOTE: cwd_len includes the trailing \0 */
         ASSERT(user_buf[cwd_len - 2] == '/');
         user_buf[cwd_len - 2] = 0; /* drop the trailing '/' */
      }

      ret = (int) cwd_len;
   }

out:
   enable_preemption();
   return ret;
}
