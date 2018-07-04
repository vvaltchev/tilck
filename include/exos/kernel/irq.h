
#pragma once

#include <exos/common/basic_defs.h>
#include <exos/kernel/hal.h>
#include <exos/kernel/interrupts.h>

void setup_irq_handling();

void irq_install_handler(u8 irq, irq_interrupt_handler h);
void irq_uninstall_handler(u8 irq);

void irq_set_mask(u8 irq_line);
void irq_clear_mask(u8 irq_line);

void debug_show_spurious_irq_count(void);
