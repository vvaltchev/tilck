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

/*
 * This function does NOT release the former `fs` and `path` and should be
 * used ONLY directly once during the initialization in main.c and during
 * fork(). For all the other cases, call process_set_cwd2_nolock().
 */
void process_set_cwd2_nolock_raw(process_info *pi, vfs_path *tp)
{
   ASSERT(tp->fs != NULL);
   ASSERT(tp->fs_path.inode != NULL);

   retain_obj(tp->fs);
   vfs_retain_inode_at(tp);
   pi->cwd2 = *tp;
}

void process_set_cwd2_nolock(vfs_path *tp)
{
   process_info *pi = get_curr_task()->pi;
   ASSERT(kmutex_is_curr_task_holding_lock(&pi->fslock));
   ASSERT(pi->cwd2.fs != NULL);
   ASSERT(pi->cwd2.fs_path.inode != NULL);

   /*
    * We have to release the inode at that path and the fs containing it, before
    * changing them with process_set_cwd2_nolock_raw().
    */

   vfs_release_inode_at(&pi->cwd2);
   release_obj(pi->cwd2.fs);
   process_set_cwd2_nolock_raw(pi, tp);
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

      process_set_cwd2_nolock(&p);

      /*
       * We need to unlock and release the fs because vfs_resolve() retained
       * and locked it.
       */
      vfs_fs_shunlock(p.fs);
      release_obj(p.fs);

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
