/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

void init_pic_8259(u8 offset1, u8 offset2);
void pic_mask_and_send_eoi(int irq);
void pic_send_eoi(int irq);
bool pic_is_spur_irq(int irq);
void irq_set_mask(int irq);
void irq_clear_mask(int irq);
