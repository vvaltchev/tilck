
#include <common/basic_defs.h>
#include <common/string_util.h>
#include <exos/kmalloc.h>
#include <exos/hal.h>
#include <exos/errno.h>
#include <exos/process.h>

#include "gdt_int.h"

static gdt_entry initial_gdt_in_bss[8];

static u32 gdt_size = ARRAY_SIZE(initial_gdt_in_bss);
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

   e->base_low = (base & 0xFFFF);
   e->base_middle = (base >> 16) & 0xFF;
   e->base_high = (base >> 24) & 0xFF;

   e->limit_low = (limit & 0xFFFF);
   e->limit_high = ((limit >> 16) & 0x0F);

   e->access = access;
   e->flags = flags;
}

int gdt_set_entry_num(u32 n, gdt_entry *e)
{
   uptr var;
   int rc = 0;
   disable_interrupts(&var);

   if (n >= gdt_size) {
      rc = -1;
      goto out;
   }

   gdt[n] = *e;

out:
   enable_interrupts(&var);
   return rc;
}

int gdt_expand(void)
{
   uptr var;
   void *old_gdt_ptr;
   const u32 new_size = gdt_size * 2;
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

int gdt_add_entry(gdt_entry *e)
{
   int rc = -1;
   uptr var;

   disable_interrupts(&var);
   {

      for (u32 n = 1; n < gdt_size; n++) {
         if (!gdt[n].access) {
            gdt[n] = *e;
            rc = n;
            break;
         }
      }

   }
   enable_interrupts(&var);
   return rc;
}

int gdt_add_ldt_entry(void *ldt_ptr, u32 size)
{
   gdt_entry e;

   gdt_set_entry(&e,
                (uptr) ldt_ptr,
                size,
                GDT_DESC_TYPE_LDT,
                GDT_GRAN_BYTE | GDT_32BIT);

   return gdt_add_entry(&e);
}

void gdt_clear_entry(u32 n)
{
   uptr var;
   disable_interrupts(&var);
   {
      bzero(&gdt[n], sizeof(gdt_entry));
   }
   enable_interrupts(&var);
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

static void DEBUG_set_thread_area(user_desc *d)
{
   printk("set_thread_area(e: %i,\n"
          "                         base: %p,\n"
          "                         lim: %p,\n"
          "                         32-bit: %u,\n"
          "                         contents: %u,\n"
          "                         re_only: %u,\n"
          "                         lim in pag: %u,\n"
          "                         seg_not_pres: %u,\n"
          "                         useable: %u)\n",
          d->entry_number,
          d->base_addr,
          d->limit,
          d->seg_32bit,
          d->contents,
          d->read_exec_only,
          d->limit_in_pages,
          d->seg_not_present,
          d->useable);
}

static int find_available_slot_in_user_task(void)
{
   task_info *curr = get_current_task();
   for (u32 i = 0; i < ARRAY_SIZE(curr->gdt_entries); i++)
      if (!curr->gdt_entries[i])
         return i;

   return -1;
}

static int get_user_task_slot_for_gdt_entry(int gdt_entry_num)
{
   task_info *curr = get_current_task();
   for (u32 i = 0; i < ARRAY_SIZE(curr->gdt_entries); i++)
      if (curr->gdt_entries[i] == gdt_entry_num)
         return i;

   return -1;
}

sptr sys_set_thread_area(user_desc *d)
{
   int rc = 0;
   gdt_entry e = {0};

   disable_preemption();
   //DEBUG_set_thread_area(d);

   if (!(d->flags == USER_DESC_FLAGS_EMPTY && !d->base_addr && !d->limit)) {
      gdt_set_entry(&e, d->base_addr, d->limit, 0, 0);
      e.s = 1;
      e.dpl = 3;
      e.d = d->seg_32bit;
      e.type |= (d->contents << 2);
      e.type |= !d->read_exec_only ? GDT_ACCESS_RW : 0;
      e.g = d->limit_in_pages;
      e.avl = d->useable;
      e.p = !d->seg_not_present;
   } else {
      /* The user passed an empty descriptor: entry_number cannot be -1 */
      if (d->entry_number == INVALID_ENTRY_NUM) {
         rc = -EINVAL;
         goto out;
      }
   }

   if (d->entry_number == INVALID_ENTRY_NUM) {

      int slot = find_available_slot_in_user_task();

      if (slot < 0) {
         rc = -ESRCH;
         goto out;
      }

      d->entry_number = gdt_add_entry(&e);

      if (d->entry_number == INVALID_ENTRY_NUM) {

         rc = gdt_expand();

         if (rc < 0) {
            rc = -ESRCH;
            goto out;
         }

         d->entry_number = gdt_add_entry(&e);
      }

      get_current_task()->gdt_entries[slot] = d->entry_number;
      goto out;
   }

   /* Handling the case where the user specified a GDT entry number */

   int slot = get_user_task_slot_for_gdt_entry(d->entry_number);

   if (slot < 0) {
      /* A GDT entry with that index has never been allocated by this task */

      if (d->entry_number >= gdt_size || gdt[d->entry_number].access) {
         /* The entry is out-of-bounds or it's used by another task */
         rc = -EINVAL;
         goto out;
      }

      /* The entry is available, now find a slot */
      slot = find_available_slot_in_user_task();

      if (slot < 0) {
         /* Unable to find a free slot in this task_info struct */
         rc = -ESRCH;
         goto out;
      }

      get_current_task()->gdt_entries[slot] = d->entry_number;
   }

   ASSERT(d->entry_number < gdt_size);

   rc = gdt_set_entry_num(d->entry_number, &e);

   /*
    * We're here because either we found a slot already containing this index
    * (therefore it must be valid) or the index is in-bounds and it is free.
    */

   ASSERT(rc == 0);

out:
   enable_preemption();
   return rc;
}
