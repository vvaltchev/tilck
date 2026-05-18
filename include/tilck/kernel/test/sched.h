/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/sched.h>

STATIC struct task *
sched_do_select_runnable_task(enum task_state curr_state, bool resched);
