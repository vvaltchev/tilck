/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/debug_utils.h>
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/modules.h>
#include <tilck/common/printk.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/sched.h>

#define TEST_VAR_VALUE 3

long test_on_exit_cb_counter = TEST_VAR_VALUE;

static void
on_exit_test_callback(struct task *ti)
{
   test_on_exit_cb_counter += 1;
}

void register_test_on_exit_callback(void)
{
   register_on_task_exit_cb(&on_exit_test_callback);
}

void unregister_test_on_exit_callback(void)
{
   unregister_on_task_exit_cb(&on_exit_test_callback);
   test_on_exit_cb_counter = TEST_VAR_VALUE;
}

static int
tilck_call_fn_0(const char *fn_name)
{
   const ulong fn_addr = find_addr_of_symbol(fn_name);

   if (!fn_addr) {
      printk("systests: function '%s' not found\n", fn_name);
      return -ENOENT;
   }

   ((void (*)(void))fn_addr)();
   return 0;
}

static int
tilck_get_var_long(const char *var_name, long *buf)
{
   const ulong var_addr = find_addr_of_symbol(var_name);

   if (!var_addr) {
      printk("systests: variable '%s' not found\n", var_name);
      return -ENOENT;
   }

   *buf = *((long*)var_addr);
   return 0;
}

static void
systests_init(void)
{
   register_tilck_cmd(TILCK_CMD_CALL_FUNC_0, &tilck_call_fn_0);
   register_tilck_cmd(TILCK_CMD_GET_VAR_LONG, &tilck_get_var_long);
   printk("Module systests initialized\n");
}

static struct module systests_module = {
   .name = "systests",
   .priority = MOD_systests_prio,
   .init = &systests_init,
};

REGISTER_MODULE(&systests_module);
