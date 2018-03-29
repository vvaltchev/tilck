
#pragma once

#include <common/basic_defs.h>
#include <exos/hal.h>
#include <exos/interrupts.h>

void irq_install();

void irq_install_handler(u8 irq, interrupt_handler h);
void irq_uninstall_handler(u8 irq);

void irq_set_mask(u8 IRQline);
void irq_clear_mask(u8 IRQline);
