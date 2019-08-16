/* SPDX-License-Identifier: BSD-2-Clause */

#define __SYSCALLS_C__

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/gcov.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/signal.h>
#include <tilck/kernel/timer.h>

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

int sys_nanosleep(const struct timespec *user_req, struct timespec *rem)
{
   u64 ticks_to_sleep = 0;
   struct timespec req;

   if (copy_from_user(&req, user_req, sizeof(req)) < 0)
      return -EFAULT;

   ticks_to_sleep += (uptr) TIMER_HZ * (uptr) req.tv_sec;
   ticks_to_sleep += (uptr) req.tv_nsec / (1000000000 / TIMER_HZ);
   kernel_sleep(ticks_to_sleep);

   // TODO (future): use HPET in order to improve the sleep precision
   // TODO (nanosleep): set rem if the call has been interrupted by a signal
   return 0;
}

static const char uname_name[] = "Tilck";
static const char uname_nodename[] = "tilck";
static const char uname_release[] = "0.01";

int sys_newuname(struct utsname *user_buf)
{
   struct utsname buf;

   memcpy(buf.sysname, uname_name, ARRAY_SIZE(uname_name));
   memcpy(buf.nodename, uname_nodename, ARRAY_SIZE(uname_nodename));
   memcpy(buf.release, uname_release, ARRAY_SIZE(uname_release));
   memcpy(buf.version, "", 1);
   memcpy(buf.machine, "i686", 5); // TODO: set the right architecture

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
   if (sig < 0 || sig >= _NSIG || tid <= 0)
      return -EINVAL;

   return send_signal(tid, sig, false);
}

int sys_tgkill(int pid /* linux: tgid */, int tid, int sig)
{
   if (pid != tid) {
      printk("sys_tgkill: pid != tid NOT SUPPORTED yet.\n");
      return -EINVAL;
   }

   if (sig < 0 || sig >= _NSIG || pid <= 0 || tid <= 0)
      return -EINVAL;

   return send_signal2(pid, tid, sig, false);
}

int sys_kill(int pid, int sig)
{
   if (pid <= 0) {
      printk("sys_kill: pid <= 0 NOT SUPPORTED yet.\n");
      return -EINVAL;
   }

   if (sig < 0 || sig >= _NSIG || pid <= 0)
      return -EINVAL;

   return send_signal(pid, sig, true);
}

uptr sys_times(struct tms *user_buf)
{
   task_info *curr = get_curr_task();
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
      return (uptr) -EBADF;

   return (uptr) get_ticks();
}

int sys_vfork()
{
   // TODO: consider actually implementing vfork().
   return sys_fork();
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

/* *************************************************************** */
/*          Tilck-specific syscalls & helper functions             */
/* *************************************************************** */

int sys_tilck_run_selftest(const char *user_selftest)
{
   int rc;
   int tid;
   char buf[256] = SELFTEST_PREFIX;

   rc = copy_str_from_user(buf + sizeof(SELFTEST_PREFIX) - 1,
                           user_selftest,
                           sizeof(buf) - sizeof(SELFTEST_PREFIX) - 2,
                           NULL);

   if (rc != 0)
      return -EFAULT;

   printk("Running function: %s()\n", buf);

   uptr addr = find_addr_of_symbol(buf);

   if (!addr)
      return -EINVAL;

   if ((tid = kthread_create((void *)addr, NULL)) < 0)
      return tid;

   kthread_join(tid);
   return 0;
}

int sys_tilck_cmd(enum tilck_testcmd_type cmd,
                  uptr a1, uptr a2, uptr a3, uptr a4)
{
   switch (cmd) {

      case TILCK_TESTCMD_RUN_SELFTEST:
         return sys_tilck_run_selftest((const char *)a1);

      case TILCK_TESTCMD_GCOV_GET_NUM_FILES:
         return sys_gcov_get_file_count();

      case TILCK_TESTCMD_GCOV_FILE_INFO:
         return sys_gcov_get_file_info((int)a1,
                                       (char *)a2,
                                       (u32) a3,
                                       (u32 *)a4);

      case TILCK_TESTCMD_GCOV_GET_FILE:
         return sys_gcov_get_file((int)a1, (char *)a2);

      case TILCK_TESTCMD_QEMU_POWEROFF:
         debug_qemu_turn_off_machine();
         return 0;

      case TILCK_TESTCMD_SET_SAT_ENABLED:
         set_sched_alive_thread_enabled((bool)a1);
         return 0;

      default:
         return -EINVAL;
   }
}
