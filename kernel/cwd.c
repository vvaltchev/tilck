/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/process.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/fs/vfs.h>

static void
set_process_str_cwd(process_info *pi, const char *path)
{
   ASSERT(kmutex_is_curr_task_holding_lock(&pi->fslock));

   size_t pl = strlen(path);
   memcpy(pi->cwd, path, pl + 1);

   if (pl > 1) {

      if (pi->cwd[pl - 1] == '/')
         pl--; /* drop the trailing slash */

      /* on the other side, pi->cwd has always a trailing '/' */
      pi->cwd[pl] = '/';
      pi->cwd[pl + 1] = 0;
   }
}

static int
getcwd_nolock(process_info *pi, char *user_buf, size_t buf_size)
{
   ASSERT(kmutex_is_curr_task_holding_lock(&pi->fslock));
   const size_t cl = strlen(pi->cwd) + 1;

   if (!user_buf || !buf_size)
      return -EINVAL;

   if (buf_size < cl)
      return -ERANGE;

   if (copy_to_user(user_buf, pi->cwd, cl))
      return -EFAULT;

   if (cl > 2) { /* NOTE: `cl` counts the trailing `\0` */
      ASSERT(user_buf[cl - 2] == '/');
      user_buf[cl - 2] = 0; /* drop the trailing '/' */
   }

   return (int) cl;
}

int sys_chdir(const char *user_path)
{
   int rc = 0;
   vfs_path p;
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
      if ((rc = vfs_resolve(orig_path, &p, false, true)))
         goto out;

      if (!p.fs_path.inode) {
         rc = -ENOENT;
         vfs_fs_shunlock(p.fs);
         release_obj(p.fs);
         goto out;
      }

      if (p.fs_path.type != VFS_DIR) {
         rc = -ENOTDIR;
         vfs_fs_shunlock(p.fs);
         release_obj(p.fs);
         goto out;
      }

      if (LIKELY(pi->cwd2.fs != NULL)) {

         /*
          * Default case: pi->cwd2 is set.
          * We have to release the inode at that path and the fs containing it
          * as well.
          */

         vfs_release_inode_at(&pi->cwd2);
         release_obj(pi->cwd2.fs);
      }

      /*
       * Here we have a vfs_path with `fs` retained. We need to retain the `fs`
       * anyway, therefore, just don't touch its ref-count. What we must do is
       * to retain the inode pointed by this vfs_path and release the shared
       * lock vfs_resolve() acquired for us on its owner fs.
       */
      vfs_retain_inode_at(&p);
      vfs_fs_shunlock(p.fs);
      pi->cwd2 = p;

      DEBUG_ONLY_UNSAFE(rc =)
         compute_abs_path(orig_path, pi->cwd, path, MAX_PATH);

      /*
       * compute_abs_path() MUST NOT fail, because we have been already able
       * to resolve the path.
       */
      ASSERT(rc == 0);

      set_process_str_cwd(pi, path);
   }

out:
   kmutex_unlock(&pi->fslock);
   return rc;
}

int sys_getcwd(char *user_buf, size_t buf_size)
{
   int rc;
   process_info *pi = get_curr_task()->pi;

   kmutex_lock(&pi->fslock);
   {
      rc = getcwd_nolock(pi, user_buf, buf_size);
   }
   kmutex_unlock(&pi->fslock);
   return rc;
}
