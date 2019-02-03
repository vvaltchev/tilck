/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/sched.h>

#include <signal.h> // system header

int send_signal(task_info *ti, int signum);
