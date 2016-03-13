
#pragma once

#include <commonDefs.h>

void irq_install_handler(int irq, void(*handler)(struct regs *r));
void irq_uninstall_handler(int irq);

extern void IRQ_set_mask(uint8_t IRQline);
extern void IRQ_clear_mask(uint8_t IRQline);

void irq_install();


