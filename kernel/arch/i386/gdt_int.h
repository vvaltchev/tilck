
#pragma once
#include <common/basic_defs.h>


#define GDT_LIMIT_MAX (0x000FFFFF)

#define GDT_GRAN_4KB (1 << 3)
#define GDT_GRAN_BYTE (0)
#define GDT_32BIT (1 << 2)
#define GDT_16BIT (0)

#define GDT_REGULAR_32BIT_SEG (GDT_GRAN_4KB | GDT_32BIT)

typedef struct
{
   u16 limit_low;
   u16 base_low;
   u8 base_middle;
   u8 access;
   u8 limit_high: 4;
   u8 flags: 4;
   u8 base_high;

} PACKED gdt_entry;

typedef struct
{
   u32 prev_tss;   /* ptr to the previous TSS: unused in ExOS */

   u32 esp0;       /* ESP to use when we change to kernel mode */
   u32 ss0;        /* SS to use when we change to kernel mode */

   /* Unused registers in ExOS (the hardware task-switch is not used) */
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
} PACKED tss_entry_t;
