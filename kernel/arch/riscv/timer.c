/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/utils.h>
#include <tilck/kernel/modules.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/datetime.h>

#define X86_PC_TIMER_IRQ           0

u32 hw_timer_setup(u32 interval)
{
   NOT_IMPLEMENTED();
}
