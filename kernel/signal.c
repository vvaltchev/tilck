/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/process.h>

int send_signal(task_info *ti, int signum)
{
   /* stub implementation: just always terminate the process */
   terminate_process(get_curr_task(), 0, signum); /* calls the scheduler */
   return 0;
}
