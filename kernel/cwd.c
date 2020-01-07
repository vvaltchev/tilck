/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/fs/vfs.h>

static void
set_process_str_cwd(struct process *pi, const char *path)
{
   ASSERT(kmutex_is_curr_task_holding_lock(&pi->fslock));

   size_t pl = strlen(path);
   memcpy(pi->str_cwd, path, pl + 1);

   if (pl > 1) {

      if (pi->str_cwd[pl - 1] == '/')
         pl--; /* drop the trailing slash */

      /* on the other side, pi->str_cwd has always a trailing '/' */
      pi->str_cwd[pl] = '/';
      pi->str_cwd[pl + 1] = 0;
   }
}

static int
getcwd_nolock(struct process *pi, char *user_buf, size_t buf_size)
{
   ASSERT(kmutex_is_curr_task_holding_lock(&pi->fslock));
   const size_t cl = strlen(pi->str_cwd) + 1;

   if (!user_buf || !buf_size)
      return -EINVAL;

   if (buf_size < cl)
      return -ERANGE;

   if (copy_to_user(user_buf, pi->str_cwd, cl))
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
void process_set_cwd2_nolock_raw(struct process *pi, struct vfs_path *tp)
{
   ASSERT(tp->fs != NULL);
   ASSERT(tp->fs_path.inode != NULL);

   retain_obj(tp->fs);
   vfs_retain_inode_at(tp);
   pi->cwd = *tp;
}

void process_set_cwd2_nolock(struct vfs_path *tp)
{
   struct process *pi = get_curr_task()->pi;
   ASSERT(kmutex_is_curr_task_holding_lock(&pi->fslock));
   ASSERT(pi->cwd.fs != NULL);
   ASSERT(pi->cwd.fs_path.inode != NULL);

   /*
    * We have to release the inode at that path and the fs containing it, before
    * changing them with process_set_cwd2_nolock_raw().
    */

   vfs_release_inode_at(&pi->cwd);
   release_obj(pi->cwd.fs);
   process_set_cwd2_nolock_raw(pi, tp);
}

int sys_chdir(const char *user_path)
{
   int rc = 0;
   struct vfs_path p;
   struct task *curr = get_curr_task();
   struct process *pi = curr->pi;
   char *orig_path = curr->args_copybuf;
   char *path = curr->args_copybuf + ARGS_COPYBUF_SIZE / 2;

   STATIC_ASSERT(ARRAY_SIZE(pi->str_cwd) == MAX_PATH);
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
         compute_abs_path(orig_path, pi->str_cwd, path, MAX_PATH);

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
   struct process *pi = get_curr_task()->pi;

   kmutex_lock(&pi->fslock);
   {
      rc = getcwd_nolock(pi, user_buf, buf_size);
   }
   kmutex_unlock(&pi->fslock);
   return rc;
}
