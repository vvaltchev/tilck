/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>
#include <tilck/common/basic_defs.h>

#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/self_tests.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/gcov.h>
#include <tilck/kernel/debug_utils.h>

typedef int (*tilck_cmd_func)();
static int sys_tilck_run_selftest(const char *user_selftest);
static int tilck_call_fn_0(const char *fn_name);
static int tilck_get_var_long(const char *var_name, long *buf);

static void *tilck_cmds[TILCK_CMD_COUNT] = {

   [TILCK_CMD_RUN_SELFTEST] = sys_tilck_run_selftest,
   [TILCK_CMD_GCOV_GET_NUM_FILES] = sys_gcov_get_file_count,
   [TILCK_CMD_GCOV_FILE_INFO] = sys_gcov_get_file_info,
   [TILCK_CMD_GCOV_GET_FILE] = sys_gcov_get_file,
   [TILCK_CMD_QEMU_POWEROFF] = debug_qemu_turn_off_machine,
   [TILCK_CMD_SET_SAT_ENABLED] = set_sched_alive_thread_enabled,
   [TILCK_CMD_DEBUG_PANEL] = NULL,
   [TILCK_CMD_TRACING_TOOL] = NULL,
   [TILCK_CMD_PS_TOOL] = NULL,
   [TILCK_CMD_DEBUGGER_TOOL] = NULL,
   [TILCK_CMD_CALL_FUNC_0] = NULL,
   [TILCK_CMD_GET_VAR_LONG] = NULL,
};

void register_tilck_cmd(int cmd_n, void *func)
{
   ASSERT(0 <= cmd_n && cmd_n < TILCK_CMD_COUNT);
   VERIFY(tilck_cmds[cmd_n] == NULL);

   tilck_cmds[cmd_n] = func;
}

static int sys_tilck_run_selftest(const char *u_selftest)
{
   int rc;
   struct self_test *se;
   char buf[96];

   if (!KERNEL_SELFTESTS)
      return -EINVAL;

   rc = copy_str_from_user(buf, u_selftest, sizeof(buf) - 1, NULL);

   if (rc != 0)
      return -EFAULT;

   se = se_find(buf);

   if (!se)
      return -EINVAL;

   printk("Running self-test: %s\n", buf);
   return se_run(se);
}

int sys_tilck_cmd(int cmd_n, ulong a1, ulong a2, ulong a3, ulong a4)
{
   tilck_cmd_func func;

   if (cmd_n >= TILCK_CMD_COUNT)
      return -EINVAL;

   *(void **)(&func) = tilck_cmds[cmd_n];

   if (!func)
      return -EINVAL;

   return func(a1, a2, a3, a4);
}
