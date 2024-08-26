/* SPDX-License-Identifier: BSD-2-Clause */

#define __SYSCALLS_C__

#include <tilck/common/basic_defs.h>
#include <tilck/common/build_info.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/signal.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/datetime.h>
#include <tilck/kernel/fs/vfs.h>

#include <linux/sched.h> // system header


#define LINUX_REBOOT_MAGIC1         0xfee1dead
#define LINUX_REBOOT_MAGIC2          672274793
#define LINUX_REBOOT_MAGIC2A          85072278
#define LINUX_REBOOT_MAGIC2B         369367448
#define LINUX_REBOOT_MAGIC2C         537993216

#define LINUX_REBOOT_CMD_RESTART     0x1234567
#define LINUX_REBOOT_CMD_RESTART2   0xa1b2c3d4
#define LINUX_REBOOT_CMD_HALT       0xcdef0123
#define LINUX_REBOOT_CMD_POWER_OFF  0x4321fedc

int sys_madvise(void *addr, size_t len, int advice)
{
   // TODO (future): consider implementing at least part of sys_madvice().
   return 0;
}

int
do_nanosleep(const struct k_timespec64 *req, struct k_timespec64 *rem)
{
   u64 ticks_to_sleep;
   u64 exp_wake_up_ticks;

   ticks_to_sleep = timespec_to_ticks(req);
   exp_wake_up_ticks = get_ticks() + ticks_to_sleep;
   kernel_sleep(ticks_to_sleep);

   /* After wake-up */
   rem->tv_sec = 0;
   rem->tv_nsec = 0;

   if (pending_signals()) {

      u64 ticks = get_ticks();

      if (ticks < exp_wake_up_ticks)
         ticks_to_timespec(exp_wake_up_ticks - ticks, rem);

      return -EINTR;
   }

   return 0;
}

int
sys_nanosleep_time32(const struct k_timespec32 *user_req,
                     struct k_timespec32 *user_rem)
{
   struct k_timespec32 req32;
   struct k_timespec64 req;
   struct k_timespec32 rem32;
   struct k_timespec64 rem;
   int rc;

   if (copy_from_user(&req32, user_req, sizeof(req)))
      return -EFAULT;

   req = (struct k_timespec64) {
      .tv_sec = req32.tv_sec,
      .tv_nsec = req32.tv_nsec,
   };

   rc = do_nanosleep(&req, &rem);

   if (user_rem) {

      rem32 = (struct k_timespec32) {
         .tv_sec = (s32) rem.tv_sec,
         .tv_nsec = rem.tv_nsec,
      };

      if (copy_to_user(user_rem, &rem32, sizeof(rem32)))
         return -EFAULT;
   }

   return rc;
}

long sys_nanosleep(const struct k_timespec64 *u_req,
                   struct k_timespec64 *u_rem)
{
   struct k_timespec64 req;
   struct k_timespec64 rem;
   int rc;

   if (copy_from_user(&req, u_req, sizeof(req)))
      return -EFAULT;

   rc = do_nanosleep(&req, &rem);

   if (u_rem) {
      if (copy_to_user(u_rem, &rem, sizeof(rem)))
         return -EFAULT;
   }

   return rc;
}

int sys_newuname(struct utsname *user_buf)
{
   struct commit_hash_and_date comm;
   struct utsname buf = {0};

   extract_commit_hash_and_date(&tilck_build_info, &comm);

   strcpy(buf.sysname, "Tilck");
   strcpy(buf.nodename, "tilck");
   strcpy(buf.version, comm.hash);
   strcpy(buf.release, tilck_build_info.ver);
   strcpy(buf.machine, tilck_build_info.arch);

   if (copy_to_user(user_buf, &buf, sizeof(struct utsname)) < 0)
      return -EFAULT;

   return 0;
}

NORETURN int sys_exit(int exit_status)
{
   terminate_process(exit_status, 0 /* term_sig */);

   /* Necessary to guarantee to the compiler that we won't return. */
   NOT_REACHED();
}

NORETURN int sys_exit_group(int status)
{
   // TODO: update when user threads are supported
   sys_exit(status);
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
         .tms_utime = (clock_t) curr->ticks.total,
         .tms_stime = (clock_t) curr->ticks.total_kernel,
         .tms_cutime = 0,
         .tms_cstime = 0,
      };

   }
   enable_preemption();

   if (copy_to_user(user_buf, &buf, sizeof(buf)) != 0)
      return (ulong) -EBADF;

   return (ulong) get_ticks();
}

int sys_getrusage(int who, struct k_rusage *user_buf)
{
   struct task *curr = get_curr_task();
   struct k_rusage buf;
   u64 utime_ticks;
   u64 stime_ticks;
   struct k_timespec64 utime;
   struct k_timespec64 stime;

   /*
    * Of course in the syscall entry point
    * interrupts are enabled
    */
   ASSERT(are_interrupts_enabled());

   if (who == RUSAGE_CHILDREN)
      /*
       * TODO: Resource usage of a process's children
       *       isn't supported yet!
       */
      return -EINVAL;

   if (who != RUSAGE_SELF && who != RUSAGE_THREAD)
      return -EINVAL;

   /*
    * Since there can only be one thread per process,
    * RUSAGE_SELF and RUSAGE_THREAD have the same meaning.
    */
   disable_interrupts_forced();
   {
      stime_ticks = curr->ticks.total_kernel;
      utime_ticks = curr->ticks.total - curr->ticks.total_kernel;
   }
   enable_interrupts_forced();

   ticks_to_timespec(utime_ticks, &utime);
   ticks_to_timespec(stime_ticks, &stime);

   buf = (struct k_rusage) {

      .ru_utime = k_ts64_to_k_timeval(utime),
      .ru_stime = k_ts64_to_k_timeval(stime),

      /* linux extentions */
      .ru_maxrss = 0,
      .ru_ixrss  = 0,
      .ru_idrss  = 0,
      .ru_isrss  = 0,
      .ru_minflt = 0,
      .ru_majflt = 0,
      .ru_nswap  = 0,
      .ru_inblock = 0,
      .ru_oublock = 0,
      .ru_msgsnd = 0,
      .ru_msgrcv = 0,
      .ru_nsignals = 0,
      .ru_nvcsw  = 0,
      .ru_nivcsw = 0,
   };

   if (copy_to_user(user_buf, &buf, sizeof(buf)))
      return -EFAULT;

   return 0;
}

int sys_fork(void *u_regs)
{
   return do_fork(u_regs, false);
}

int sys_vfork(void *u_regs)
{
   return do_fork(u_regs, true);
}

long sys_clone(regs_t *u_regs, ulong clone_flags, ulong newsp,
               int *u_parent_tidptr, int *u_child_tidptr, ulong tls)
{
   // TODO: Add full support for clone()

   if (clone_flags == SIGCHLD)
      return sys_fork(u_regs);
   else if (clone_flags == (CLONE_VFORK | CLONE_VM | SIGCHLD))
      return sys_vfork(u_regs);
   else
      return -ENOSYS;
}

static int
stop_all_user_tasks(void *task, void *unused)
{
   struct task *ti = task;

   if (!is_kernel_thread(ti) && ti != get_curr_task()) {
      printk("Stopping TID %d\n", ti->tid);
      ti->stopped = true;
   }

   return 0;
}

static void
kernel_shutdown(void)
{
   extern volatile bool __in_kernel_shutdown;

   if (__in_kernel_shutdown)
      return;

   __in_kernel_shutdown = true;
   printk("The system is shutting down.\n");

   disable_preemption();
   {
      iterate_over_tasks(&stop_all_user_tasks, NULL);
   }
   enable_preemption();

   /* Give kernel threads a chance to run and complete their work */
   for (int i = 0; i < 10; i++)
      kernel_yield();

   printk("Shutdown complete.\n");
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

   switch (cmd) {

      case LINUX_REBOOT_CMD_RESTART:
      case LINUX_REBOOT_CMD_RESTART2:
         kernel_shutdown();
         reboot();
         break;

      case LINUX_REBOOT_CMD_HALT:
         kernel_shutdown();
         disable_interrupts_forced();
         while (true) { halt(); }
         break;

      case LINUX_REBOOT_CMD_POWER_OFF:
         kernel_shutdown();
         poweroff();
         break;

      default:
         return -EINVAL;
   }

   return 0;
}

int sys_sched_yield(void)
{
   kernel_yield();
   return 0;
}

int sys_utimes(const char *u_path, const struct k_timeval u_times[2])
{
   struct k_timeval ts[2];
   struct k_timespec64 new_ts[2];
   char *path = get_curr_task()->args_copybuf;

   if (copy_str_from_user(path, u_path, MAX_PATH, NULL))
      return -EFAULT;

   if (u_times) {

      if (copy_from_user(ts, u_times, sizeof(ts)))
         return -EFAULT;

      new_ts[0] = (struct k_timespec64) {
         .tv_sec = ts[0].tv_sec,
         .tv_nsec = ((long)ts[0].tv_usec) * 1000,
      };

      new_ts[1] = (struct k_timespec64) {
         .tv_sec = ts[1].tv_sec,
         .tv_nsec = ((long)ts[1].tv_usec) * 1000,
      };

   } else {

      /*
       * If `u_times` is NULL, the access and modification times of the file
       * are set to the current time.
       */

      real_time_get_timespec(&new_ts[0]);
      new_ts[1] = new_ts[0];
   }

   return vfs_utimens(path, new_ts);
}

int sys_utime32(const char *u_path, const struct k_utimbuf *u_times)
{
   struct k_utimbuf ts;
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

long sys_utimensat(int dirfd, const char *u_path,
                   struct k_timespec64 utimes[2], int flags)
{
   //TODO: Add support for fully utimensat() features

   struct k_timeval ts[2];
   struct k_timespec64 new_ts[2];
   char *path = get_curr_task()->args_copybuf;

   if (flags || (dirfd != AT_FDCWD))
      return -ENOSYS;

   if (copy_str_from_user(path, u_path, MAX_PATH, NULL))
      return -EFAULT;

   if (utimes) {

      if (copy_from_user(ts, utimes, sizeof(ts)))
         return -EFAULT;

   } else {

      /*
       * If `u_times` is NULL, the access and modification times of the file
       * are set to the current time.
       */

      real_time_get_timespec(&new_ts[0]);
      new_ts[1] = new_ts[0];
   }

   return vfs_utimens(path, new_ts);
}

int sys_utimensat_time32(int dirfd, const char *u_path,
                         const struct k_timespec32 times[2], int flags)
{
   // TODO (future): consider implementing sys_utimensat() [modern]
   return -ENOSYS;
}

int sys_futimesat_time32(int dirfd, const char *u_path,
                         const struct k_timeval times[2])
{
   // TODO (future): consider implementing sys_futimesat_time32() [obsolete]
   return -ENOSYS;
}

int sys_socketcall(int call, ulong *args)
{
   return -ENOSYS;
}

