/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

struct tss_entry {

   u32 prev_tss;   /* ptr to the previous TSS: unused in Tilck */

   u32 esp0;       /* ESP to use when we change to kernel mode */
   u32 ss0;        /* SS to use when we change to kernel mode */

   /* Unused registers in Tilck (the hardware task-switch is not used) */
   u32 esp1;
   u32 ss1;
   u32 esp2;
   u32 ss2;
   u32 cr3;
   u32 eip;
   u32 eflags;
   u32 eax;
   u32 ecx;
   u32 edx;
   u32 ebx;
   u32 esp;
   u32 ebp;
   u32 esi;
   u32 edi;
   u32 es;
   u32 cs;
   u32 ss;
   u32 ds;
   u32 fs;
   u32 gs;
   u32 ldt;
   u16 trap;
   u16 iomap_base;

} PACKED;
