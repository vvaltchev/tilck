
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <exos/kmalloc.h>
#include <exos/hal.h>

#include "gdt_int.h"

static gdt_entry initial_gdt_in_bss[64];

static int gdt_size = ARRAY_SIZE(initial_gdt_in_bss);
static gdt_entry *gdt = initial_gdt_in_bss;

/*
 * ExOS does use i386's tasks because they do not exist in many architectures.
 * Therefore, we have just need a single TSS entry.
 */
static tss_entry_t tss_entry;

void gdt_set_entry(gdt_entry *e,
                   uptr base,
                   uptr limit,
                   u8 access,
                   u8 flags)
{
   ASSERT(limit <= GDT_LIMIT_MAX); /* limit is only 20 bits */
   ASSERT(flags <= 0xF); /* flags is 4 bits */
   ASSERT(!are_interrupts_enabled());

   e->base_low = (base & 0xFFFF);
   e->base_middle = (base >> 16) & 0xFF;
   e->base_high = (base >> 24) & 0xFF;

   e->limit_low = (limit & 0xFFFF);
   e->limit_high = ((limit >> 16) & 0x0F);

   e->access = access;
   e->flags = flags;
}

int gdt_expand(int new_size)
{
   ASSERT(new_size > gdt_size);

   uptr var;
   void *old_gdt_ptr;
   void *new_gdt = kzmalloc(sizeof(gdt_entry) * new_size);

   if (!new_gdt)
      return -1;

   disable_interrupts(&var);
   {
      old_gdt_ptr = gdt;
      memcpy(new_gdt, gdt, sizeof(gdt_entry) * gdt_size);
      gdt = new_gdt;
      gdt_size = new_size;
      load_gdt(new_gdt, new_size);
   }
   enable_interrupts(&var);

   if (old_gdt_ptr != initial_gdt_in_bss)
      kfree(old_gdt_ptr);

   return 0;
}

int gdt_add_entry(uptr base,
                  uptr limit,
                  u8 access,
                  u8 flags)
{
   ASSERT(!are_interrupts_enabled());

   for (int n = 1; n < gdt_size; n++) {
      if (!gdt[n].access) {
         gdt_set_entry(&gdt[n], base, limit, access, flags);
         return n;
      }
   }

   return -1; /* the caller has to handle this by using gdt_expand() */
}

int gdt_add_ldt_entry(void *ldt_ptr, u32 size)
{
   return gdt_add_entry((uptr) ldt_ptr,
                        size,
                        GDT_DESC_TYPE_LDT,
                        GDT_GRAN_BYTE | GDT_32BIT);
}

void gdt_clear_entry(int n)
{
   ASSERT(!are_interrupts_enabled());
   ASSERT(n > 0);

   bzero(&gdt[n], sizeof(gdt_entry));
}

void set_kernel_stack(u32 stack)
{
   uptr var;
   disable_interrupts(&var);
   {
      tss_entry.ss0 = X86_KERNEL_DATA_SEL; /* Kernel stack segment = data seg */
      tss_entry.esp0 = stack;
      wrmsr(MSR_IA32_SYSENTER_ESP, stack);
   }
   enable_interrupts(&var);
}

void load_gdt(gdt_entry *gdt, u32 entries_count)
{
   ASSERT(!are_interrupts_enabled());

   struct {

      u16 size_minus_one;
      gdt_entry *gdt_vaddr;

   } PACKED gdt_ptr = { sizeof(gdt_entry) * entries_count - 1, gdt };

   asmVolatile("lgdt (%0)"
               : /* no output */
               : "q" (&gdt_ptr)
               : "memory");
}

void load_tss(u32 entry_index_in_gdt, u32 dpl)
{
   ASSERT(!are_interrupts_enabled());
   ASSERT(dpl <= 3); /* descriptor privilege level [0..3] */

   /*
    * Inline assembly comments: here we need to use %w0 instead of %0
    * beacuse 'ltr' requires a 16-bit register, like AX. That's what does
    * the 'w' modifier [https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html].
    */

   asmVolatile("ltr %w0"
               : /* no output */
               : "q" (X86_SELECTOR(entry_index_in_gdt, TABLE_GDT, dpl))
               : "memory");
}

void setup_segmentation(void)
{
   ASSERT(!are_interrupts_enabled());

   /* Our NULL descriptor */
   gdt_set_entry(&gdt[0], 0, 0, 0, 0);

   /* Kernel code segment */
   gdt_set_entry(&gdt[1],
                 0,              /* base addr */
                 GDT_LIMIT_MAX,  /* full 4-GB segment */
                 GDT_ACC_REG | GDT_ACCESS_PL0 | GDT_ACCESS_RW | GDT_ACCESS_EX,
                 GDT_GRAN_4KB | GDT_32BIT);

   /* Kernel data segment */
   gdt_set_entry(&gdt[2],
                 0,
                 GDT_LIMIT_MAX,
                 GDT_ACC_REG | GDT_ACCESS_PL0 | GDT_ACCESS_RW,
                 GDT_GRAN_4KB | GDT_32BIT);

   /* Usermode code segment */
   gdt_set_entry(&gdt[3],
                 0,
                 GDT_LIMIT_MAX,
                 GDT_ACC_REG | GDT_ACCESS_PL3 | GDT_ACCESS_RW | GDT_ACCESS_EX,
                 GDT_GRAN_4KB | GDT_32BIT);

   /* Usermode data segment */
   gdt_set_entry(&gdt[4],
                 0,
                 GDT_LIMIT_MAX,
                 GDT_ACC_REG | GDT_ACCESS_PL3 | GDT_ACCESS_RW,
                 GDT_GRAN_4KB | GDT_32BIT);

   /* GDT entry for our TSS */
   gdt_set_entry(&gdt[5],
                 (uptr) &tss_entry,   /* TSS addr */
                 sizeof(tss_entry),   /* limit: struct TSS size */
                 GDT_DESC_TYPE_TSS,
                 GDT_GRAN_BYTE | GDT_32BIT);

   load_gdt(gdt, gdt_size);
   load_tss(5 /* TSS index in GDT */, 3 /* priv. level */);
}
