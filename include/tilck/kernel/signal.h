/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/sched.h>

#include <signal.h> // system header

#define TILCK_SIG_DEFAULT  ((uptr)0u)
#define TILCK_SIG_IGNORE   ((uptr)-1)

void send_signal(task_info *ti, int signum);
