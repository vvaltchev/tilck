/* SPDX-License-Identifier: BSD-2-Clause */

#define __SYSCALLS_C__

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/signal.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/fs/vfs.h>

#define LINUX_REBOOT_MAGIC1         0xfee1dead
#define LINUX_REBOOT_MAGIC2          672274793
#define LINUX_REBOOT_MAGIC2A          85072278
#define LINUX_REBOOT_MAGIC2B         369367448
#define LINUX_REBOOT_MAGIC2C         537993216

#define LINUX_REBOOT_CMD_RESTART     0x1234567
#define LINUX_REBOOT_CMD_RESTART2   0xa1b2c3d4

int sys_madvise(void *addr, size_t len, int advice)
{
   // TODO (future): consider implementing at least part of sys_madvice().
   return 0;
}

int
do_nanosleep(const struct k_timespec64 *req)
{
   u64 ticks_to_sleep = 0;

   ticks_to_sleep += (ulong) TIMER_HZ * (ulong) req->tv_sec;
   ticks_to_sleep += (ulong) req->tv_nsec / (1000000000 / TIMER_HZ);
   kernel_sleep(ticks_to_sleep);

   // TODO (future): use HPET in order to improve the sleep precision
   // TODO (nanosleep): set rem if the call has been interrupted by a signal
   return 0;
}

int
sys_nanosleep_time32(const struct k_timespec32 *user_req,
                     struct k_timespec32 *rem)
{
   struct k_timespec32 req32;
   struct k_timespec64 req;
   int rc;

   if (copy_from_user(&req32, user_req, sizeof(req)))
      return -EFAULT;

   req = (struct k_timespec64) {
      .tv_sec = req32.tv_sec,
      .tv_nsec = req32.tv_nsec,
   };

   if ((rc = do_nanosleep(&req)))
      return rc;

   bzero(&req32, sizeof(req32));

   if (copy_to_user(rem, &req32, sizeof(req32)))
      return -EFAULT;

   return 0;
}

const char uname_name[] = "Tilck";
const char uname_nodename[] = "tilck";
const char uname_release[] = VER_MAJOR_STR "." VER_MINOR_STR "." VER_PATCH_STR;
const char uname_arch[] = ARCH_GCC_TC;
const char commit_hash[65] = {0};

int sys_newuname(struct utsname *user_buf)
{
   struct utsname buf;

   memcpy(buf.sysname, uname_name, ARRAY_SIZE(uname_name));
   memcpy(buf.nodename, uname_nodename, ARRAY_SIZE(uname_nodename));
   memcpy(buf.release, uname_release, ARRAY_SIZE(uname_release));
   memcpy(buf.version, commit_hash, strlen(commit_hash) + 1);
   memcpy(buf.machine, uname_arch, ARRAY_SIZE(uname_arch));

   if (copy_to_user(user_buf, &buf, sizeof(struct utsname)) < 0)
      return -EFAULT;

   return 0;
}

NORETURN int sys_exit(int exit_status)
{
   disable_preemption();
   terminate_process(get_curr_task(), exit_status, 0 /* term_sig */);

   /* Necessary to guarantee to the compiler that we won't return. */
   NOT_REACHED();
}

NORETURN int sys_exit_group(int status)
{
   // TODO: update when user threads are supported
   sys_exit(status);
}


/* NOTE: deprecated syscall */
int sys_tkill(int tid, int sig)
{
   if (!IN_RANGE(sig, 0, _NSIG) || tid <= 0)
      return -EINVAL;

   return send_signal(tid, sig, false);
}

int sys_tgkill(int pid /* linux: tgid */, int tid, int sig)
{
   if (pid != tid) {
      printk("sys_tgkill: pid != tid NOT SUPPORTED yet.\n");
      return -EINVAL;
   }

   if (!IN_RANGE(sig, 0, _NSIG) || pid <= 0 || tid <= 0)
      return -EINVAL;

   return send_signal2(pid, tid, sig, false);
}


int sys_kill(int pid, int sig)
{
   if (!IN_RANGE(sig, 0, _NSIG))
      return -EINVAL;

   if (pid == 0)
      return send_signal_to_group(get_curr_proc()->pgid, sig);

   if (pid == -1) {

      /*
       * In theory, pid == -1, means:
       *    "sig is sent to every process for which the calling process has
       *     permission to send signals, except for process 1 (init)".
       *
       * But Tilck does not have a permission model nor multi-user support.
       * So, what to do here? Kill everything except init? Mmh, not sure.
       * It looks acceptable for the moment to just kill all the processes in
       * the current session.
       */
      return send_signal_to_session(get_curr_proc()->sid, sig);
   }

   if (pid < -1)
      return send_signal_to_group(-pid, sig);

   /* pid > 0 */
   return send_signal(pid, sig, true);
}

ulong sys_times(struct tms *user_buf)
{
   struct task *curr = get_curr_task();
   struct tms buf;

   // TODO (threads): when threads are supported, update sys_times()
   // TODO: consider supporting tms_cutime and tms_cstime in sys_times()

   disable_preemption();
   {

      buf = (struct tms) {
         .tms_utime = (clock_t) curr->total_ticks,
         .tms_stime = (clock_t) curr->total_kernel_ticks,
         .tms_cutime = 0,
         .tms_cstime = 0,
      };

   }
   enable_preemption();

   if (copy_to_user(user_buf, &buf, sizeof(buf)) != 0)
      return (ulong) -EBADF;

   return (ulong) get_ticks();
}

int sys_fork(void)
{
   return do_fork(false);
}

int sys_vfork(void)
{
   return do_fork(true);
}

int sys_reboot(u32 magic, u32 magic2, u32 cmd, void *arg)
{
   if (magic != LINUX_REBOOT_MAGIC1)
      return -EINVAL;

   if (magic2 != LINUX_REBOOT_MAGIC2  &&
       magic2 != LINUX_REBOOT_MAGIC2A &&
       magic2 != LINUX_REBOOT_MAGIC2B &&
       magic2 != LINUX_REBOOT_MAGIC2C)
   {
      return -EINVAL;
   }

   if (cmd == LINUX_REBOOT_CMD_RESTART ||
       cmd == LINUX_REBOOT_CMD_RESTART2)
   {
      printk("Restarting system\n");
      reboot();
   }

   return -EINVAL;
}

int sys_sched_yield(void)
{
   kernel_yield();
   return 0;
}

int sys_utimes(const char *u_path, const struct timeval u_times[2])
{
   struct timeval ts[2];
   struct k_timespec64 new_ts[2];
   char *path = get_curr_task()->args_copybuf;

   if (copy_from_user(ts, u_times, sizeof(ts)))
      return -EFAULT;

   if (copy_str_from_user(path, u_path, MAX_PATH, NULL))
      return -EFAULT;

   new_ts[0] = (struct k_timespec64) {
      .tv_sec = ts[0].tv_sec,
      .tv_nsec = ((long)ts[0].tv_usec) * 1000,
   };

   new_ts[1] = (struct k_timespec64) {
      .tv_sec = ts[1].tv_sec,
      .tv_nsec = ((long)ts[1].tv_usec) * 1000,
   };

   return vfs_utimens(path, new_ts);
}

int sys_utime(const char *u_path, const struct utimbuf *u_times)
{
   struct utimbuf ts;
   struct k_timespec64 new_ts[2];
   char *path = get_curr_task()->args_copybuf;

   if (copy_from_user(&ts, u_times, sizeof(ts)))
      return -EFAULT;

   if (copy_str_from_user(path, u_path, MAX_PATH, NULL))
      return -EFAULT;

   new_ts[0] = (struct k_timespec64) {
      .tv_sec = ts.actime,
      .tv_nsec = 0,
   };

   new_ts[1] = (struct k_timespec64) {
      .tv_sec = ts.modtime,
      .tv_nsec = 0,
   };

   return vfs_utimens(path, new_ts);
}

int sys_utimensat_time32(int dirfd, const char *u_path,
                         const struct k_timespec32 times[2], int flags)
{
   // TODO (future): consider implementing sys_utimensat() [modern]
   return -ENOSYS;
}

int sys_futimesat(int dirfd, const char *u_path,
                  const struct timeval times[2])
{
   // TODO (future): consider implementing sys_futimesat() [obsolete]
   return -ENOSYS;
}

int sys_fsync(int fd)
{
   return 0;
}

int sys_fdatasync(int fd)
{
   return 0;
}

int sys_socketcall(int call, ulong *args)
{
   return -ENOSYS;
}
