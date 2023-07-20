/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/kernel/sched.h>

long cmd_exit_cb_var_0 = 3;

static void
cmd_register_on_exit(struct task *ti)
{
   cmd_exit_cb_var_0 += 1;
}

static void
cmd_unregister_on_exit(struct task *ti)
{
   cmd_exit_cb_var_0 += 1;
}

void cmd_exit_cb_func_0(void)
{
   register_on_task_exit_cb(&cmd_register_on_exit);
   unregister_on_task_exit_cb(&cmd_unregister_on_exit);
}
