/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/boot.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>
#include <tilck/kernel/modules.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/sched.h>

u32 spur_irq_count = 0;
u32 unhandled_irq_count[MAX_IRQ_NUM];
