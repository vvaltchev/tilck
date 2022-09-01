/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_debug.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/irq.h>

#include "idt_int.h"
#include "../generic_x86/pic.h"


/*
 * We first remap the interrupt controllers, and then we install
 * the appropriate ISRs to the correct entries in the IDT. This
 * is just like installing the exception handlers.
 */

void init_irq_handling(void)
{
   ASSERT(!are_interrupts_enabled());
   init_pic_8259(32, 40);

   for (int i = 0; i < ARRAY_SIZE(irq_handlers_lists); i++) {

      idt_set_entry(32 + (u8)i,
                    irq_entry_points[i],
                    X86_KERNEL_CODE_SEL,
                    IDT_FLAG_PRESENT | IDT_FLAG_INT_GATE | IDT_FLAG_DPL0);

      irq_set_mask(i);
   }
}
