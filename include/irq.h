
#pragma once

#include <commonDefs.h>


void idt_set_gate(unsigned char num,
                  unsigned long base,
                  unsigned short sel,
                  unsigned char flags);

void irq_install();

void irq_install_handler(int irq, void(*handler)(struct regs *r));
void irq_uninstall_handler(int irq);

void IRQ_set_mask(uint8_t IRQline);
void IRQ_clear_mask(uint8_t IRQline);
