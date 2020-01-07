/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/self_tests.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/gcov.h>
#include <tilck/kernel/debug_utils.h>

typedef int (*tilck_cmd_func)();
static int sys_tilck_run_selftest(const char *user_selftest);

static void *tilck_cmds[] = {

   [TILCK_CMD_RUN_SELFTEST] = sys_tilck_run_selftest,
   [TILCK_CMD_GCOV_GET_NUM_FILES] = sys_gcov_get_file_count,
   [TILCK_CMD_GCOV_FILE_INFO] = sys_gcov_get_file_info,
   [TILCK_CMD_GCOV_GET_FILE] = sys_gcov_get_file,
   [TILCK_CMD_QEMU_POWEROFF] = debug_qemu_turn_off_machine,
   [TILCK_CMD_SET_SAT_ENABLED] = set_sched_alive_thread_enabled,
   [TILCK_CMD_DEBUG_PANEL] = NULL,
};

void register_tilck_cmd(int cmd_n, void *func)
{
   ASSERT(0 <= cmd_n && cmd_n < TILCK_CMD_COUNT);
   VERIFY(tilck_cmds[cmd_n] == NULL);

   tilck_cmds[cmd_n] = func;
}

static int sys_tilck_run_selftest(const char *user_selftest)
{
   int rc;
   int tid;
   uptr addr;
   char buf[256] = SELFTEST_PREFIX;

   rc = copy_str_from_user(buf + sizeof(SELFTEST_PREFIX) - 1,
                           user_selftest,
                           sizeof(buf) - sizeof(SELFTEST_PREFIX) - 2,
                           NULL);

   if (rc != 0)
      return -EFAULT;

   if (!KERNEL_SELFTESTS)
      return -EINVAL;

   if (!(addr = find_addr_of_symbol(buf)))
      return -EINVAL;

   printk("Running function: %s()\n", buf);

   if ((tid = kthread_create(se_internal_run, KTH_ALLOC_BUFS, (void*)addr)) < 0)
      return tid;

   kthread_join(tid);
   return 0;
}

int sys_tilck_cmd(int cmd_n, uptr a1, uptr a2, uptr a3, uptr a4)
{
   tilck_cmd_func func;

   if (cmd_n >= TILCK_CMD_COUNT)
      return -EINVAL;

   *(void **)(&func) = tilck_cmds[cmd_n];

   if (!func)
      return -EINVAL;

   return func(a1, a2, a3, a4);
}
