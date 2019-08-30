/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/process_int.h>
#include <tilck/kernel/paging.h>

#include "double_fault.h"
#include "gdt_int.h"
#include "idt_int.h"

static int double_fault_tss_num;
static void double_fault_handler(void);

static tss_entry_t double_fault_tss_entry =
{
   .esp0 = (uptr)kernel_initial_stack,
   .ss0 = X86_KERNEL_DATA_SEL,
   .cr3 = 0,                             /* updated later */
   .eip = (uptr)&double_fault_handler,
   .eflags = 0x2,
   .es = X86_KERNEL_DATA_SEL,
   .cs = X86_KERNEL_CODE_SEL,
   .ss = X86_KERNEL_DATA_SEL,
   .ds = X86_KERNEL_DATA_SEL,
   .fs = X86_KERNEL_DATA_SEL,
   .gs = X86_KERNEL_DATA_SEL,
};

void on_first_pdir_update(void)
{
   double_fault_tss_entry.cr3 = read_cr3();
}

void register_double_fault_tss_entry(void)
{
   gdt_entry e;

   gdt_set_entry(&e,
                 (uptr)&double_fault_tss_entry,
                 sizeof(double_fault_tss_entry),
                 GDT_DESC_TYPE_TSS,
                 GDT_GRAN_BYTE | GDT_32BIT);

   double_fault_tss_num = gdt_add_entry(&e);

   if (double_fault_tss_num < 0)
      panic("Unable to add a GDT entry for the double fault TSS");

   /* Set the initial value for CR3 */
   double_fault_tss_entry.cr3 = read_cr3();

   /* Install the task gate for the double fault */
   idt_set_entry(FAULT_DOUBLE_FAULT,
                 NULL, /* handler (offset): zero */
                 (u16)double_fault_tss_num,
                 IDT_FLAG_PRESENT | IDT_FLAG_TASK_GATE | IDT_FLAG_DPL0);
}

static void double_fault_handler(void)
{
   while (1) halt();
}
