
#pragma once

#include <common/basic_defs.h>
#include <exos/hal.h>
#include <exos/interrupts.h>

void setup_irq_handling();

void irq_install_handler(u8 irq, interrupt_handler h);
void irq_uninstall_handler(u8 irq);

void irq_set_mask(u8 irq_line);
void irq_clear_mask(u8 irq_line);
