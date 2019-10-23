/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

/*
 * x86 IDT entry flags:
 *
 * +-----------------+--------------+-----------+--------------------+
 * | Present [1 bit] | DPL [2 bits] | S [1 bit] | Gate type [4 bits] |
 * +-----------------+--------------+-----------+--------------------+
 *
 * Difference between an interrupt gate and a trap gate: for interrupt gates,
 * interrupts are automatically disabled and eflags is saved (iret restores
 * it as well).
 *
 * DPL: the minimum level the current descriptor should have to call this gate.
 * This allow kernel to prevent user mode to trigger interrupts, simulating
 * hardware interrupts or faults. In order to "int 0x80" to work, it needs to
 * have, indeed, DPL = 3.
 */

#define IDT_FLAG_INT_GATE  (0xE << 0)  /* 32 bit interrupt gate */
#define IDT_FLAG_TRAP_GATE (0xF << 0)  /* 32 bit trap gate */
#define IDT_FLAG_TASK_GATE (0x5 << 0)  /* 32 bit task gate */
#define IDT_FLAG_DPL0      (0 << 5)
#define IDT_FLAG_DPL1      (1 << 5)
#define IDT_FLAG_DPL2      (2 << 5)
#define IDT_FLAG_DPL3      (3 << 5)
#define IDT_FLAG_PRESENT   (1 << 7)

struct idt_entry {

   u16 offset_low;
   u16 selector;
   u8 always0;
   u8 flags;
   u16 offset_high;

} PACKED;

void load_idt(struct idt_entry *entries, u16 entries_count);
void idt_set_entry(u8 num, void *handler, u16 selector, u8 flags);
