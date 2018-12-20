/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>

void init_sched(void);
task_info *get_task(int tid);
void task_change_state(task_info *ti, enum task_state new_state);
