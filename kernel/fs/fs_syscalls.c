/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/fs/kernelfs.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/fault_resumable.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/pipe.h>

#include <fcntl.h>      // system header

static inline bool is_fd_in_valid_range(int fd)
{
   return IN_RANGE(fd, 0, MAX_HANDLES);
}

static int get_free_handle_num_ge(struct process *pi, int ge)
{
   ASSERT(kmutex_is_curr_task_holding_lock(&pi->fslock));

   for (int free_fd = ge; free_fd < MAX_HANDLES; free_fd++)
      if (!pi->handles[free_fd])
         return free_fd;

   return -1;
}

static int get_free_handle_num(struct process *pi)
{
   return get_free_handle_num_ge(pi, 0);
}

/*
 * Even if getting the fs_handle this way is safe now, it won't be anymore
 * after thread-support is added to the kernel. For example, a thread might
 * work with given handle while another closes it.
 *
 * TODO: introduce a ref-count in the fs_base_handle struct and function like
 * put_fs_handle() or rename both to something like acquire/release_fs_handle.
 */
fs_handle get_fs_handle(int fd)
{
   struct task *curr = get_curr_task();
   fs_handle handle = NULL;

   kmutex_lock(&curr->pi->fslock);

   if (is_fd_in_valid_range(fd) && curr->pi->handles[fd])
      handle = curr->pi->handles[fd];

   kmutex_unlock(&curr->pi->fslock);
   return handle;
}


int sys_open(const char *u_path, int flags, mode_t mode)
{
   int ret, free_fd;
   struct task *curr = get_curr_task();
   char *path = curr->args_copybuf;
   size_t written = 0;
   fs_handle h = NULL;

   STATIC_ASSERT((ARGS_COPYBUF_SIZE / 2) >= MAX_PATH);

   /* Apply the umask upfront */
   mode &= ~curr->pi->umask;

   if ((ret = duplicate_user_path(path, u_path, MAX_PATH, &written)))
      return ret;

   kmutex_lock(&curr->pi->fslock);

   if ((free_fd = get_free_handle_num(curr->pi)) < 0)
      goto no_fds;

   if ((ret = vfs_open(path, &h, flags, mode)) < 0)
      goto end;

   ASSERT(h != NULL);

   curr->pi->handles[free_fd] = h;
   ret = free_fd;

end:
   kmutex_unlock(&curr->pi->fslock);
   return ret;

no_fds:
   ret = -EMFILE;
   goto end;
}

int sys_creat(const char *u_path, mode_t mode)
{
   return sys_open(u_path, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

int sys_unlink(const char *u_path)
{
   struct task *curr = get_curr_task();
   char *path = curr->args_copybuf;
   size_t written = 0;
   int ret;

   if ((ret = duplicate_user_path(path, u_path, MAX_PATH, &written)))
      return ret;

   return vfs_unlink(path);
}

int sys_rmdir(const char *u_path)
{
   struct task *curr = get_curr_task();
   char *path = curr->args_copybuf;
   size_t written = 0;
   int ret;

   if ((ret = duplicate_user_path(path, u_path, MAX_PATH, &written)))
      return ret;

   return vfs_rmdir(path);
}

int sys_close(int fd)
{
   struct task *curr = get_curr_task();
   fs_handle handle;
   int ret = 0;

   if (!(handle = get_fs_handle(fd)))
      return -EBADF;

   kmutex_lock(&curr->pi->fslock);
   {
      vfs_close(handle);
      curr->pi->handles[fd] = NULL;
   }
   kmutex_unlock(&curr->pi->fslock);
   return ret;
}

int sys_mkdir(const char *u_path, mode_t mode)
{
   struct task *curr = get_curr_task();
   char *path = curr->args_copybuf;
   size_t written = 0;
   int ret;

   /* Apply the umask upfront */
   mode &= ~curr->pi->umask;

   if ((ret = duplicate_user_path(path, u_path, MAX_PATH, &written)))
      return ret;

   return vfs_mkdir(path, mode);
}

int sys_read(int fd, void *u_buf, size_t count)
{
   int ret;
   struct task *curr = get_curr_task();
   fs_handle handle;

   handle = get_fs_handle(fd);

   if (!handle)
      return -EBADF;

   /*
    * NOTE:
    *
    * From `man 2 read`:
    *
    *    On  Linux,  read()  (and similar system calls) will transfer at most
    *    0x7ffff000 (2,147,479,552) bytes, returning the number of bytes
    *    actually transferred. (This is true on both 32-bit and 64-bit systems.)
    *
    * This means that it's perfectly fine to use `int` instead of ssize_t as
    * return type of sys_read().
    */

   count = MIN(count, IO_COPYBUF_SIZE);
   ret = (int) vfs_read(handle, curr->io_copybuf, count);

   if (ret > 0) {
      if (copy_to_user(u_buf, curr->io_copybuf, (size_t)ret) < 0) {
         // Do we have to rewind the stream in this case? It don't think so.
         ret = -EFAULT;
         goto end;
      }
   }

end:
   return ret;
}

int sys_write(int fd, const void *u_buf, size_t count)
{
   struct task *curr = get_curr_task();
   fs_handle handle;

   if (!(handle = get_fs_handle(fd)))
      return -EBADF;

   count = MIN(count, IO_COPYBUF_SIZE);

   if (copy_from_user(curr->io_copybuf, u_buf, count))
      return -EFAULT;

   return (int)vfs_write(handle, (char *)curr->io_copybuf, count);
}

int sys_ioctl(int fd, uptr request, void *argp)
{
   fs_handle handle = get_fs_handle(fd);

   if (!handle)
      return -EBADF;

   return vfs_ioctl(handle, request, argp);
}

int sys_writev(int fd, const struct iovec *u_iov, int u_iovcnt)
{
   struct task *curr = get_curr_task();
   const u32 iovcnt = (u32) u_iovcnt;
   fs_handle handle;
   int rc, ret = 0;

   if (u_iovcnt <= 0)
      return -EINVAL;

   if (sizeof(struct iovec) * iovcnt > ARGS_COPYBUF_SIZE)
      return -EINVAL;

   rc = copy_from_user(curr->args_copybuf,
                       u_iov,
                       sizeof(struct iovec) * iovcnt);

   if (rc != 0)
      return -EFAULT;

   if (!(handle = get_fs_handle(fd)))
      return -EBADF;

   vfs_exlock(handle);

   const struct iovec *iov = (const struct iovec *)curr->args_copybuf;

   for (u32 i = 0; i < iovcnt; i++) {

      rc = sys_write(fd, iov[i].iov_base, iov[i].iov_len);

      if (rc < 0) {
         ret = rc;
         break;
      }

      ret += rc;

      if (rc < (sptr)iov[i].iov_len) {
         // For some reason (perfectly legit) we couldn't write the whole
         // user data (i.e. network card's buffers are full).
         break;
      }
   }

   vfs_exunlock(handle);
   return ret;
}

int sys_readv(int fd, const struct iovec *u_iov, int u_iovcnt)
{
   struct task *curr = get_curr_task();
   const u32 iovcnt = (u32) u_iovcnt;
   fs_handle handle;
   int rc, ret = 0;

   if (u_iovcnt <= 0)
      return -EINVAL;

   if (sizeof(struct iovec) * iovcnt > ARGS_COPYBUF_SIZE)
      return -EINVAL;

   rc = copy_from_user(curr->args_copybuf,
                       u_iov,
                       sizeof(struct iovec) * iovcnt);

   if (rc != 0)
      return -EFAULT;

   if (!(handle = get_fs_handle(fd)))
      return -EBADF;

   vfs_shlock(handle);

   const struct iovec *iov = (const struct iovec *)curr->args_copybuf;

   for (u32 i = 0; i < iovcnt; i++) {

      rc = sys_read(fd, iov[i].iov_base, iov[i].iov_len);

      if (rc < 0) {
         ret = rc;
         break;
      }

      ret += rc;

      if (rc < (sptr)iov[i].iov_len)
         break; // Not enough data to fill all the user buffers.
   }

   vfs_shunlock(handle);
   return ret;
}

static int
call_vfs_stat64(const char *u_path,
                struct stat64 *u_statbuf,
                bool res_last_sl)
{
   struct task *curr = get_curr_task();
   char *path = curr->args_copybuf;
   struct stat64 statbuf;
   int rc = 0;

   rc = copy_str_from_user(path, u_path, MAX_PATH, NULL);

   if (rc < 0)
      return -EFAULT;

   if (rc > 0)
      return -ENAMETOOLONG;

   if ((rc = vfs_stat64(path, &statbuf, res_last_sl)))
      return rc;

   if (copy_to_user(u_statbuf, &statbuf, sizeof(struct stat64)))
      rc = -EFAULT;

   return rc;
}

int sys_stat64(const char *u_path, struct stat64 *u_statbuf)
{
   return call_vfs_stat64(u_path, u_statbuf, true);
}

int sys_lstat64(const char *u_path, struct stat64 *u_statbuf)
{
   return call_vfs_stat64(u_path, u_statbuf, false);
}

int sys_fstat64(int fd, struct stat64 *u_statbuf)
{
   struct stat64 statbuf;
   fs_handle h;
   int rc = 0;

   if (!(h = get_fs_handle(fd)))
      return -EBADF;

   if ((rc = vfs_fstat64(h, &statbuf)))
      return rc;

   if (copy_to_user(u_statbuf, &statbuf, sizeof(struct stat64)))
      rc = -EFAULT;

   return rc;
}

int sys_symlink(const char *u_target, const char *u_linkpath)
{
   struct task *curr     = get_curr_task();
   char *target        = curr->args_copybuf + (ARGS_COPYBUF_SIZE / 4) * 0;
   char *linkpath      = curr->args_copybuf + (ARGS_COPYBUF_SIZE / 4) * 1;
   int rc = 0;

   STATIC_ASSERT(ARGS_COPYBUF_SIZE / 4 >= MAX_PATH);

   rc = copy_str_from_user(target, u_target, MAX_PATH, NULL);

   if (rc < 0)
      return -EFAULT;

   if (rc > 0)
      return -ENAMETOOLONG;

   rc = copy_str_from_user(linkpath, u_linkpath, MAX_PATH, NULL);

   if (rc < 0)
      return -EFAULT;

   if (rc > 0)
      return -ENAMETOOLONG;

   if (!*target || !*linkpath)
      return -ENOENT; /* target or linkpath is an empty string */

   return vfs_symlink(target, linkpath);
}

int sys_readlink(const char *u_pathname, char *u_buf, size_t u_bufsize)
{
   struct task *curr = get_curr_task();
   char *path = curr->args_copybuf + (ARGS_COPYBUF_SIZE / 4) * 0;
   char *buf       = curr->args_copybuf + (ARGS_COPYBUF_SIZE / 4) * 1;
   size_t ret_bs;
   int rc;

   STATIC_ASSERT(ARGS_COPYBUF_SIZE / 4 >= MAX_PATH);

   rc = copy_str_from_user(path, u_pathname, MAX_PATH, NULL);

   if (rc < 0)
      return -EFAULT;

   if (rc > 0)
      return -ENAMETOOLONG;

   rc = vfs_readlink(path, buf);

   if (rc < 0)
      return rc;

   ret_bs = (size_t) rc;
   rc = copy_to_user(u_buf, buf, MIN(ret_bs, u_bufsize));

   if (rc < 0)
      return -EFAULT;

   return (int) ret_bs;
}

int sys_truncate64(const char *u_path, s64 len)
{
   struct task *curr = get_curr_task();
   char *path = curr->args_copybuf;
   int rc;

   if (len < 0)
      return -EINVAL;

   rc = copy_str_from_user(path, u_path, MAX_PATH, NULL);

   if (rc < 0)
      return -EFAULT;

   if (rc > 0)
      return -ENAMETOOLONG;

   // NOTE: truncating the 64-bit length to a pointer-size integer
   return vfs_truncate(path, (offt)len);
}

int sys_ftruncate64(int fd, s64 len)
{
   fs_handle h;

   if (!(h = get_fs_handle(fd)))
      return -EBADF;

   // NOTE: truncating the 64-bit length to a pointer-size integer
   return vfs_ftruncate(h, (offt)len);
}

int sys_llseek(int fd, size_t off_hi, size_t off_low, u64 *result, u32 whence)
{
   const s64 off64 = (s64)(((u64)off_hi << 32) | off_low);
   fs_handle handle;
   s64 new_off;

   STATIC_ASSERT(sizeof(new_off) >= sizeof(offt));

   if (!(handle = get_fs_handle(fd)))
      return -EBADF;

   new_off = vfs_seek(handle, off64, (int)whence);

   if (new_off < 0)
      return (int) new_off; /* return back vfs_seek's error */

   if (copy_to_user(result, &new_off, sizeof(*result)))
      return -EBADF;

   return 0;
}

int sys_getdents64(int fd, struct linux_dirent64 *u_dirp, u32 buf_size)
{
   fs_handle handle;

   if (!(handle = get_fs_handle(fd)))
      return -EBADF;

   return vfs_getdents64(handle, u_dirp, buf_size);
}

int sys_access(const char *pathname, int mode)
{
   // TODO: check mode and file r/w flags.
   return 0;
}

/*
 * NOTE: on Tilck, this syscall _can_ return -ENOMEM, while Linux would return
 * -EBADF or -EMFILE in the same case. See the comment below.
 */
int sys_dup2(int oldfd, int newfd)
{
   int rc;
   fs_handle old_h, new_h;
   struct task *curr = get_curr_task();

   if (!is_fd_in_valid_range(oldfd))
      return -EBADF;

   if (!is_fd_in_valid_range(newfd))
      return -EBADF;

   if (newfd == oldfd)
      return -EINVAL;

   kmutex_lock(&curr->pi->fslock);

   if (!(old_h = get_fs_handle(oldfd))) {
      rc = -EBADF;
      goto out;
   }

   new_h = get_fs_handle(newfd);

   if (new_h) {

      /*
       * CORNER CASE: In general, the new handle should be available, but the
       * linux kernel allows the user code to pass also an IN-USE handle: in
       * that case the behavior is to just silently close that handle, before
       * reusing it.
       */
      vfs_close(new_h);
      new_h = NULL;
   }

   if ((rc = vfs_dup(old_h, &new_h))) {

      /*
       * if (rc == -ENOMEM)
       *    rc = -EBADF;
       *
       * [BE_NICE] Creating a new file handle means allocating memory, no matter
       * if that happens every time like in Tilck or it's deferred using a sort
       * of dynamic array like the Linux kernel does. A memory allocation can
       * always fail, even when that's highly unlikely. In such cases, the
       * kernel _must_ return -ENOMEM.
       *
       * Unfortunately, according to the POSIX standard dup() and dup2() cannot
       * fail with -ENOMEM. Just, it's not part of the POSIX interface and it's
       * not clear to me why. In the Linux kernel, in the out-of-memory case,
       * dup() returns -EMFILE while dup2() returns -EBADF.
       *
       * Tilck tries to be more honest and returns -ENOMEM.
       */
      goto out;
   }

   curr->pi->handles[newfd] = new_h;
   rc = newfd;

out:
   kmutex_unlock(&curr->pi->fslock);
   return rc;
}

int sys_dup(int oldfd)
{
   int rc = -EMFILE, free_fd;
   struct process *pi = get_curr_task()->pi;

   kmutex_lock(&pi->fslock);
   {
      free_fd = get_free_handle_num(pi);

      if (is_fd_in_valid_range(free_fd))
         rc = sys_dup2(oldfd, free_fd);
   }
   kmutex_unlock(&pi->fslock);
   return rc;
}

static void debug_print_fcntl_command(int cmd)
{
   switch (cmd) {

      case F_DUPFD:
         printk("fcntl: F_DUPFD\n");
         break;
      case F_DUPFD_CLOEXEC:
         printk("fcntl: F_DUPFD_CLOEXEC\n");
         break;
      case F_GETFD:
         printk("fcntl: F_GETFD\n");
         break;
      case F_SETFD:
         printk("fcntl: F_SETFD\n");
         break;
      case F_GETFL:
         printk("fcntl: F_GETFL\n");
         break;
      case F_SETFL:
         printk("fcntl: F_SETFL\n");
         break;
      case F_SETLK:
         printk("fcntl: F_SETLK\n");
         break;
      case F_SETLKW:
         printk("fcntl: F_SETLKW\n");
         break;
      case F_GETLK:
         printk("fcntl: F_GETLK\n");
         break;

      /* Skipping several other commands */

      default:
         printk("fcntl: unknown command\n");
   }
}

void close_cloexec_handles(struct process *pi)
{
   kmutex_lock(&pi->fslock);

   for (u32 i = 0; i < MAX_HANDLES; i++) {

      struct fs_handle_base *h = pi->handles[i];

      if (h && (h->fd_flags & FD_CLOEXEC)) {
         vfs_close(h);
         pi->handles[i] = NULL;
      }
   }

   kmutex_unlock(&pi->fslock);
}

int sys_fcntl64(int fd, int cmd, int arg)
{
   int rc = 0;
   struct task *curr = get_curr_task();
   struct fs_handle_base *hb;

   hb = get_fs_handle(fd);

   if (!hb)
      return -EBADF;

   switch (cmd) {

      case F_DUPFD:
         {
            kmutex_lock(&curr->pi->fslock);
            int new_fd = get_free_handle_num_ge(curr->pi, arg);
            rc = sys_dup2(fd, new_fd);
            kmutex_unlock(&curr->pi->fslock);
            return rc;
         }

      case F_DUPFD_CLOEXEC:
         {
            kmutex_lock(&curr->pi->fslock);
            int new_fd = get_free_handle_num_ge(curr->pi, arg);
            rc = sys_dup2(fd, new_fd);
            if (!rc) {
               struct fs_handle_base *h2 = get_fs_handle(new_fd);
               ASSERT(h2 != NULL);
               h2->fd_flags |= FD_CLOEXEC;
            }
            kmutex_unlock(&curr->pi->fslock);
            return rc;
         }

      case F_SETFD:
         hb->fd_flags = arg;
         break;

      case F_GETFD:
         return hb->fd_flags;

      case F_SETFL:
         hb->fl_flags = arg;
         break;

      case F_GETFL:
         return hb->fl_flags;

      default:
         rc = vfs_fcntl(hb, cmd, arg);
   }

   if (rc == -EINVAL) {
      printk("[fcntl64] Ignored unknown cmd %d\n", cmd);
   }

   return rc;
}

static int
do_chown(const char *u_path, int owner, int group, bool reslink)
{
   struct task *curr = get_curr_task();
   char *path = curr->args_copybuf;
   int rc;

   rc = copy_str_from_user(path, u_path, MAX_PATH, NULL);

   if (rc < 0)
      return -EFAULT;

   if (rc > 0)
      return -ENAMETOOLONG;

   return vfs_chown(path, owner, group, reslink);
}

int sys_chown(const char *u_path, int owner, int group)
{
   return do_chown(u_path, owner, group, true);
}

int sys_lchown(const char *u_path, int owner, int group)
{
   return do_chown(u_path, owner, group, false);
}

int sys_fchown(int fd, uid_t owner, gid_t group)
{
   struct fs_handle_base *hb;
   hb = get_fs_handle(fd);

   if (!hb)
      return -EBADF;

   if (!(hb->fs->flags & VFS_FS_RW))
      return -EROFS;

   return (owner == 0 && group == 0) ? 0 : -EPERM;
}

int sys_sync()
{
   /* Do nothing, for the moment */
   return 0;
}

int sys_chmod(const char *u_path, mode_t mode)
{
   struct task *curr = get_curr_task();
   char *path = curr->args_copybuf;
   int rc;

   rc = copy_str_from_user(path, u_path, MAX_PATH, NULL);

   if (rc < 0)
      return -EFAULT;

   if (rc > 0)
      return -ENAMETOOLONG;

   return vfs_chmod(path, mode);
}

int sys_fchmod(int fd, mode_t mode)
{
   struct fs_handle_base *hb;
   hb = get_fs_handle(fd);

   if (!hb)
      return -EBADF;

   if (!(hb->fs->flags & VFS_FS_RW))
      return -EROFS;

   return vfs_fchmod(hb, mode);
}

static int
call_rename_or_link(const char *u_oldpath,
                    const char *u_newpath,
                    int (*vfs_func)(const char *, const char *))
{
   struct task *curr = get_curr_task();
   char *oldpath = curr->args_copybuf;
   char *newpath = curr->args_copybuf + MAX_PATH;
   int rc1, rc2;

   STATIC_ASSERT(ARGS_COPYBUF_SIZE >= 2 * MAX_PATH);

   rc1 = copy_str_from_user(oldpath, u_oldpath, MAX_PATH, NULL);
   rc2 = copy_str_from_user(newpath, u_newpath, MAX_PATH, NULL);

   if (rc1 < 0 || rc2 < 0)
      return -EFAULT;

   if (rc1 > 0 || rc2 > 0)
      return -ENAMETOOLONG;

   return vfs_func(oldpath, newpath);
}

int sys_rename(const char *u_oldpath, const char *u_newpath)
{
   return call_rename_or_link(u_oldpath, u_newpath, &vfs_rename);
}

int sys_link(const char *u_oldpath, const char *u_newpath)
{
   return call_rename_or_link(u_oldpath, u_newpath, &vfs_link);
}

int sys_pipe(int u_pipefd[2])
{
   return sys_pipe2(u_pipefd, 0);
}

int sys_pipe2(int u_pipefd[2], int flags)
{
   struct task *curr = get_curr_task();
   fs_handle read_h = NULL;
   fs_handle write_h = NULL;
   struct pipe *p = NULL;
   int fds[2];
   int ret = 0;

   if (flags != 0)
      return -EINVAL;

   kmutex_lock(&curr->pi->fslock);

   if (!(p = create_pipe())) {

      /*
       * [BE_NICE] According to the POSIX standard, pipe() cannot fail with
       * -ENOMEM. However, it can fail with -ENFILE or -EMFILE, both of which
       * are about numeric limits, system-wide or per-process, it doesn't
       * matter. Also -ENFILE might mean, according to POSIX, that:
       *
       * << The user hard limit on memory that can be allocated for pipes has
       *    been reached and the caller is not privileged. >>
       *
       * That implies that there _must be_ such a hard limit and, because we
       * cannot fail with -ENOMEM, such memory for pipes must be also allocated
       * in advance, which is very bad practically.
       *
       * The situation is similar to the case of dup() and dup2() [see the
       * comment in sys_dup2()] and what the Linux kernel does in case of real
       * out-of-memory it's simply lying by failing with -ENFILE here.
       *
       * Tilck tries to be more honest even at the price of breaking a little
       * the POSIX standard, by returning -ENOMEM here.
       */
      goto no_mem;
   }

   if ((fds[0] = get_free_handle_num(curr->pi)) < 0)
      goto no_fds;

   if (!(read_h = pipe_create_read_handle(p)))
      goto fault;

   curr->pi->handles[fds[0]] = read_h;

   if ((fds[1] = get_free_handle_num(curr->pi)) < 0)
      goto no_fds;

   if (!(write_h = pipe_create_write_handle(p)))
      goto fault;

   curr->pi->handles[fds[1]] = write_h;

   if (copy_to_user(u_pipefd, fds, sizeof(fds)))
      goto fault;

end:
   kmutex_unlock(&curr->pi->fslock);
   return ret;

err_end:

   if (read_h) {
      curr->pi->handles[fds[0]] = NULL;
      kfs_destroy_handle(read_h);
   }

   if (write_h) {
      curr->pi->handles[fds[1]] = NULL;
      kfs_destroy_handle(write_h);
   }

   if (p) {
      destroy_pipe(p);
   }

   goto end;

fault:
   ret = -EFAULT;
   goto err_end;

no_mem:
   ret = -ENOMEM;
   goto err_end;

no_fds:
   ret = -EMFILE;
   goto err_end;
}
