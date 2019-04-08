/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/process.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/errno.h>

sptr sys_chdir(const char *user_path)
{
   sptr rc = 0;
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

   disable_preemption();
   {
      rc = compute_abs_path(orig_path, pi->cwd, path, MAX_PATH);

      if (rc != 0)
         goto out;

      fs_handle h = NULL;
      rc = vfs_open(path, &h, 0, O_RDONLY);

      if (rc < 0)
         goto out; /* keep the same rc */

      ASSERT(h != NULL);

      rc = vfs_stat64(h, &statbuf);

      if (rc < 0) {
         vfs_close(h);
         goto out;
      }

      vfs_close(h);

      if (!S_ISDIR(statbuf.st_mode)) {
         rc = -ENOTDIR;
         goto out;
      }

      u32 pl = (u32)strlen(path);
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
   enable_preemption();
   return rc;
}

sptr sys_getcwd(char *user_buf, size_t buf_size)
{
   sptr ret;
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

      ret = (sptr) cwd_len;
   }

out:
   enable_preemption();
   return ret;
}
