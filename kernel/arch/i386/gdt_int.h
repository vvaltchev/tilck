
#pragma once
#include <common/basic_defs.h>

/* Limit is 20 bit */
#define GDT_LIMIT_MAX (0x000FFFFF)

/* GDT 'flags' (4 bits) */
#define GDT_GRAN_4KB (1 << 3)
#define GDT_GRAN_BYTE (0)
#define GDT_32BIT (1 << 2)
#define GDT_16BIT (0)

/* GDT 'access' flags (8 bits) */

#define GDT_ACCESS_ACC     (1 << 0)  /*
                                      * Accessed bit. The CPU sets it to 1 when
                                      * the segment is accessed.
                                      */

#define GDT_ACCESS_RW      (1 << 1)  /*
                                      * For code segments it means readable,
                                      * otherwise the segment is unreadable.
                                      * [code segments cannot be writable.]
                                      *
                                      * For data segments, it means writable.
                                      */

#define GDT_ACCESS_DC      (1 << 2)  /*
                                      * Direction/Conforming bit
                                      *
                                      * For data segments: direction.
                                      *   0 the segment grows up (regular)
                                      *   1 the segment grown down
                                      *
                                      * For code segments: conforming.
                                      *
                                      *   0 the code can be executed only from
                                      *     the ring set in PL.
                                      *
                                      *   1 the code can be executed also from
                                      *     a lower priv. level. E.g. code in
                                      *     ring 3 can far-jump to conforming
                                      *     code in ring 2.
                                      */

#define GDT_ACCESS_EX       (1 << 3)  /*
                                       * Executable bit.
                                       *   0 means a data segment
                                       *   1 means a code segment
                                       */

#define GDT_ACCESS_S        (1 << 4)   /*
                                        * Descriptor type.
                                        *   0 means system (e.g. TSS, LDT)
                                        *   1 means regular (code/data)
                                        */

#define GDT_ACCESS_PL0      (0 << 5)   /* Ring 0 */
#define GDT_ACCESS_PL1      (1 << 5)   /* Ring 1 */
#define GDT_ACCESS_PL2      (2 << 5)   /* Ring 2 */
#define GDT_ACCESS_PL3      (3 << 5)   /* Ring 3 */

#define GDT_ACCESS_PRESENT  (1 << 7)   /* Must be set for valid segments */


/* An useful shortcut for saving some space */
#define GDT_ACC_REG (GDT_ACCESS_PRESENT | GDT_ACCESS_S)

typedef struct
{
   u16 limit_low;
   u16 base_low;
   u8 base_middle;

   union {

      struct {
         u8 type: 4;  /* EX + DC + RW + ACC */
         u8 s : 1;    /* 0 = system desc, 1 = regular desc */
         u8 dpl : 2;  /* desc privilege level */
         u8 p : 1;    /* present */
      };

      u8 access;
   };

   union {

      struct {
         u8 lim_high: 4;
         u8 avl : 1; /* available bit */
         u8 l : 1;   /* 64-bit segment (IA-32e only) */
         u8 d : 1;   /* default operation size. 0 = 16 bit, 1 = 32 bit */
         u8 g : 1;   /* granularity: 0 = byte, 1 = 4 KB */
      };

      struct {
         u8 limit_high: 4;
         u8 flags: 4;
      };
   };

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

void load_gdt(gdt_entry *gdt, u32 entries_count);
void gdt_set_entry(gdt_entry *e, uptr base, uptr limit, u8 access, u8 flags);
int gdt_add_entry(uptr base, uptr limit, u8 access, u8 flags);
NODISCARD int gdt_expand(int new_size);
