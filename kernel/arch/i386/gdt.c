/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/user.h>

#include "gdt_int.h"

static gdt_entry initial_gdt_in_bss[8];

static u32 gdt_size = ARRAY_SIZE(initial_gdt_in_bss);
static gdt_entry *gdt = initial_gdt_in_bss;

/*
 * Tilck does use i386's tasks because they do not exist in many architectures.
 * Therefore, we have just need a single TSS entry.
 */
static tss_entry_t tss_entry;

static void load_gdt(gdt_entry *gdt, u32 entries_count);


static void
gdt_set_entry(gdt_entry *e,
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

static int gdt_set_entry_num(u32 n, gdt_entry *e)
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

static NODISCARD int gdt_expand(void)
{
   uptr var;
   void *old_gdt_ptr;
   const u32 old_gdt_size = gdt_size;
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
      kfree2(old_gdt_ptr, old_gdt_size);

   return 0;
}

static int gdt_add_entry(gdt_entry *e)
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

static int gdt_add_ldt_entry(void *ldt_ptr, u32 size)
{
   gdt_entry e;

   gdt_set_entry(&e,
                (uptr) ldt_ptr,
                size,
                GDT_DESC_TYPE_LDT,
                GDT_GRAN_BYTE | GDT_32BIT);

   return gdt_add_entry(&e);
}

static void gdt_real_clear_entry(u32 n)
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

static void load_gdt(gdt_entry *gdt, u32 entries_count)
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
   printk(NO_PREFIX "set_thread_area(e: %i,\n"
                    "                base: %p,\n"
                    "                lim: %p,\n"
                    "                32-bit: %u,\n",
                    d->entry_number,
                    d->base_addr,
                    d->limit,
                    d->seg_32bit);

   printk(NO_PREFIX "                contents: %u,\n"
                    "                re_only: %u,\n"
                    "                lim in pag: %u,\n",
                    d->contents,
                    d->read_exec_only,
                    d->limit_in_pages);

   printk(NO_PREFIX "                seg_not_pres: %u,\n"
                    "                useable: %u)\n",
                    d->seg_not_present,
                    d->useable);
}

static int find_available_slot_in_user_task(void)
{
   task_info *curr = get_curr_task();
   for (u32 i = 0; i < ARRAY_SIZE(curr->arch.gdt_entries); i++)
      if (!curr->arch.gdt_entries[i])
         return i;

   return -1;
}

static int get_user_task_slot_for_gdt_entry(int gdt_entry_num)
{
   task_info *curr = get_curr_task();
   for (u32 i = 0; i < ARRAY_SIZE(curr->arch.gdt_entries); i++)
      if (curr->arch.gdt_entries[i] == gdt_entry_num)
         return i;

   return -1;
}

static void gdt_set_slot_in_task(task_info *ti, int slot, int gdt_index)
{
   ti->arch.gdt_entries[slot] = gdt_index;
}

void gdt_clear_entry(int index)
{
   gdt_real_clear_entry(index);
}

sptr sys_set_thread_area(user_desc *ud)
{
   int rc = 0;
   gdt_entry e = {0};
   user_desc dc;

   rc = copy_from_user(&dc, ud, sizeof(user_desc));

   if (rc != 0)
      return -EFAULT;

   //DEBUG_set_thread_area(&dc);

   disable_preemption();

   if (!(dc.flags == USER_DESC_FLAGS_EMPTY && !dc.base_addr && !dc.limit)) {
      gdt_set_entry(&e, dc.base_addr, dc.limit, 0, 0);
      e.s = 1;
      e.dpl = 3;
      e.d = dc.seg_32bit;
      e.type |= (dc.contents << 2);
      e.type |= !dc.read_exec_only ? GDT_ACCESS_RW : 0;
      e.g = dc.limit_in_pages;
      e.avl = dc.useable;
      e.p = !dc.seg_not_present;
   } else {
      /* The user passed an empty descriptor: entry_number cannot be -1 */
      if (dc.entry_number == INVALID_ENTRY_NUM) {
         rc = -EINVAL;
         goto out;
      }
   }

   if (dc.entry_number == INVALID_ENTRY_NUM) {

      int slot = find_available_slot_in_user_task();

      if (slot < 0) {
         rc = -ESRCH;
         goto out;
      }

      dc.entry_number = gdt_add_entry(&e);

      if (dc.entry_number == INVALID_ENTRY_NUM) {

         rc = gdt_expand();

         if (rc < 0) {
            rc = -ESRCH;
            goto out;
         }

         dc.entry_number = gdt_add_entry(&e);
      }

      gdt_set_slot_in_task(get_curr_task(), slot, dc.entry_number);
      goto out;
   }

   /* Handling the case where the user specified a GDT entry number */

   int slot = get_user_task_slot_for_gdt_entry(dc.entry_number);

   if (slot < 0) {
      /* A GDT entry with that index has never been allocated by this task */

      if (dc.entry_number >= gdt_size || gdt[dc.entry_number].access) {
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

      gdt_set_slot_in_task(get_curr_task(), slot, dc.entry_number);
   }

   ASSERT(dc.entry_number < gdt_size);

   rc = gdt_set_entry_num(dc.entry_number, &e);

   /*
    * We're here because either we found a slot already containing this index
    * (therefore it must be valid) or the index is in-bounds and it is free.
    */

   ASSERT(rc == 0);

out:
   enable_preemption();

   if (!rc) {

      /*
       * Positive case: we get here with rc = SUCCESS, now flush back the
       * the user_desc struct (we might have changed its entry_number).
       */
      rc = copy_to_user(ud, &dc, sizeof(user_desc));

      if (rc < 0)
         rc = -EFAULT;
   }

   return rc;
}
