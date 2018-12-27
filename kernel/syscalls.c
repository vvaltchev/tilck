
#define __SYSCALLS_C__

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/gcov.h>
#include <tilck/kernel/debug_utils.h>

sptr sys_rt_sigprocmask(/* args ignored at the moment */)
{
   // TODO: implement sys_rt_sigprocmask
   // printk("rt_sigprocmask\n");
   return 0;
}

sptr sys_nanosleep(const struct timespec *user_req, struct timespec *rem)
{
   u64 ticks_to_sleep = 0;
   struct timespec req;

   if (copy_from_user(&req, user_req, sizeof(req)) < 0)
      return -EFAULT;

   ticks_to_sleep += TIMER_HZ * req.tv_sec;
   ticks_to_sleep += req.tv_nsec / (1000000000 / TIMER_HZ);
   kernel_sleep(ticks_to_sleep);

   // TODO (future): use HPET in order to improve the sleep precision
   // TODO (nanosleep): set rem if the call has been interrupted by a signal
   return 0;
}

static const char uname_name[] = "Tilck";
static const char uname_nodename[] = "tilck";
static const char uname_release[] = "0.01";

sptr sys_newuname(struct utsname *user_buf)
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

sptr sys_tilck_run_selftest(const char *user_selftest)
{
   int rc;
   char buf[256] = SELFTEST_PREFIX;

   rc = copy_str_from_user(buf + strlen(buf),
                           user_selftest,
                           sizeof(buf) - strlen(buf) - 1,
                           NULL);

   if (rc != 0)
      return -EFAULT;

   printk("Running function: %s()\n", buf);

   uptr addr = find_addr_of_symbol(buf);

   if (!addr)
      return -EINVAL;

   task_info *ti = kthread_create((void *)addr, NULL);

   if (!ti)
      return -ENOMEM;

   return 0;
}

sptr sys_tilck_cmd(enum tilck_testcmd_type cmd,
                   uptr a1, uptr a2, uptr a3, uptr a4)
{
   switch (cmd) {

      case TILCK_TESTCMD_RUN_SELFTEST:
         return sys_tilck_run_selftest((const char *)a1);

      case TILCK_TESTCMD_DUMP_COVERAGE: // TODO: drop this!
         gcov_dump_coverage();
         return 0;

      case TILCK_TESTCMD_GCOV_GET_NUM_FILES:
         return sys_gcov_dump_coverage();

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

      default:
         break;
   }
   return -EINVAL;
}
