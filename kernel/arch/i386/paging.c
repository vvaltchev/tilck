/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_mm.h>
#include <tilck_gen_headers/mod_fb.h>

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/paging.h>
#include <tilck/kernel/paging_hw.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/signal.h>
#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/vdso.h>
#include <tilck/kernel/cmdline.h>

#include <tilck/mods/tracing.h>

#include "paging_int.h"
#include "../generic_x86/paging_generic_x86.h"

#include <sys/mman.h>      // system header

pdir_t *__kernel_pdir;
static char kpdir_buf[sizeof(pdir_t)] ALIGNED_AT(PAGE_SIZE);

static ALWAYS_INLINE page_table_t *
pdir_get_page_table(pdir_t *pdir, u32 i)
{
   return KERNEL_PA_TO_VA(pdir->entries[i].ptaddr << PAGE_SHIFT);
}

bool handle_potential_cow(void *context)
{
   regs_t *r = context;
   u32 vaddr;

   if ((r->err_code & PAGE_FAULT_FL_COW) != PAGE_FAULT_FL_COW)
      return false;

   asmVolatile("movl %%cr2, %0" : "=r"(vaddr));

   const u32 pt_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 pd_index = (vaddr >> BIG_PAGE_SHIFT);
   const void *const page_vaddr = (void *)(vaddr & PAGE_MASK);
   page_table_t *pt = pdir_get_page_table(get_curr_pdir(), pd_index);

   if (!(pt->pages[pt_index].avail & PAGE_COW_ORIG_RW))
      return false; /* Not a COW page */

   const u32 orig_page_paddr = (u32)
      pt->pages[pt_index].pageAddr << PAGE_SHIFT;

   if (pf_ref_count_get(orig_page_paddr) == 1) {

      /* This page is not shared anymore. No need for copying it. */

#if DEBUG_CHECKS
      const ulong paddr = (ulong)
         pt->pages[pt_index].pageAddr << PAGE_SHIFT;

      ASSERT(KERNEL_PA_TO_VA(paddr) != &zero_page);
#endif

      pt->pages[pt_index].rw = true;
      pt->pages[pt_index].avail = 0;
      invalidate_page_hw(vaddr);
      return true;
   }

   // Allocate a new page.
   void *new_page_vaddr = kmalloc(PAGE_SIZE);

   if (!new_page_vaddr) {

      // Out-of-memory case
      struct task *curr = get_curr_task();

      if (!curr->running_in_kernel) {

         // The task was not running in kernel: we can safely kill it.
         printk("Out-of-memory: killing pid %d\n", get_curr_pid());
         send_signal(get_curr_pid(), SIGKILL, SIG_FL_PROCESS | SIG_FL_FAULT);
         return true;

      } else {

         // We cannot kill a task running in kernel during a CoW page fault
         // In this case (but in the one above too), Linux puts the process to
         // sleep, while the OOM killer runs and frees some memory.
         panic("Out-of-memory: can't copy a CoW page [pid %d]", get_curr_pid());
      }
   }

   ASSERT(IS_PAGE_ALIGNED(new_page_vaddr));

   // Copy page's contents
   memcpy32(new_page_vaddr, page_vaddr, PAGE_SIZE / 4);

   // Get the paddr of the new page
   const ulong paddr = KERNEL_VA_TO_PA(new_page_vaddr);

   // A just-allocated pageframe MUST have ref-count == 0
   ASSERT(pf_ref_count_get(paddr) == 0);

   // Increase the ref-count of the new pageframe
   pf_ref_count_inc(paddr);

   // Decrease the ref-count of the original pageframe.
   pf_ref_count_dec(orig_page_paddr);

   // Re-map the vaddr to its new (writable) pageframe
   pt->pages[pt_index].pageAddr = SHR_BITS(paddr, PAGE_SHIFT, u32);
   pt->pages[pt_index].rw = true;
   pt->pages[pt_index].avail = 0;

   invalidate_page_hw(vaddr);
   return true;
}

static void kernel_page_fault_panic(regs_t *r, u32 vaddr, bool rw, bool p)
{
   long off = 0;
   const char *sym_name = find_sym_at_addr_safe(r->eip, &off, NULL);
   panic("PAGE FAULT in attempt to %s %p from %s%s\nEIP: %p [%s + %d]\n",
         rw ? "WRITE" : "READ",
         vaddr,
         "kernel",
         !p ? " (NON present)." : ".",
         r->eip, sym_name ? sym_name : "???", off);
}

void handle_page_fault_int(regs_t *r)
{
   u32 vaddr;
   asmVolatile("movl %%cr2, %0" : "=r"(vaddr));

   bool p  = !!(r->err_code & PAGE_FAULT_FL_PRESENT);
   bool rw = !!(r->err_code & PAGE_FAULT_FL_RW);
   bool us = !!(r->err_code & PAGE_FAULT_FL_US);
   int sig = SIGSEGV;
   struct user_mapping *um;

   if (!us) {
      /*
       * Tilck does not support kernel-space page faults caused by the kernel,
       * while it allows user-space page faults caused by kernel (CoW pages).
       * Therefore, such a fault is necessary caused by a bug.
       * We have to panic.
       */
      kernel_page_fault_panic(r, vaddr, rw, p);
   }

   um = process_get_user_mapping((void *)vaddr);

   if (um) {

      /*
       * Call vfs_handle_fault() only if in first place the mapping allowed
       * writing or if it didn't but the memory access type was a READ.
       */
      if (!!(um->prot & PROT_WRITE) || !rw) {

         if (vfs_handle_fault(um, (void *)vaddr, p, rw))
            return;

         sig = SIGBUS;
      }
   }

   if (KRN_PAGE_FAULT_PRINTK) {

      printk("[%d] USER PAGE FAULT in attempt to %s %p%s\n",
             get_curr_pid(),
             rw ? "WRITE" : "READ", TO_PTR(vaddr),
             !p ? " (NON present)." : ".");

      printk("EIP: %p\n", TO_PTR(r->eip));

      if (get_curr_proc()->debug_cmdline)
         printk("Cmdline: %s\n", get_curr_proc()->debug_cmdline);
   }

   trace_printk(
      5, "USER PAGE FAULT in attempt to %s %p%s, EIP: %p, Cmdline: '%s'",
      rw ? "WRITE" : "READ", TO_PTR(vaddr),
      !p ? " (NON present)" : "",
      TO_PTR(r->eip),
      get_curr_proc()->debug_cmdline
   );

   send_signal(get_curr_tid(), sig, SIG_FL_PROCESS | SIG_FL_FAULT);
}

bool is_mapped(pdir_t *pdir, void *vaddrp)
{
   page_table_t *pt;
   const ulong vaddr = (ulong) vaddrp;
   const u32 pt_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 pd_index = (vaddr >> BIG_PAGE_SHIFT);

   page_dir_entry_t *e = &pdir->entries[pd_index];

   if (!e->present)
      return false;

   if (e->psize) /* 4-MB page */
      return e->present;

   pt = KERNEL_PA_TO_VA(pdir->entries[pd_index].ptaddr << PAGE_SHIFT);
   return pt->pages[pt_index].present;
}

bool is_rw_mapped(pdir_t *pdir, void *vaddrp)
{
   page_table_t *pt;
   const ulong vaddr = (ulong) vaddrp;
   const u32 pt_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 pd_index = (vaddr >> BIG_PAGE_SHIFT);

   page_dir_entry_t *e = &pdir->entries[pd_index];
   page_t page;

   if (!e->present)
      return false;

   if (e->psize) /* 4-MB page */
      return e->present && e->rw;

   pt = KERNEL_PA_TO_VA(pdir->entries[pd_index].ptaddr << PAGE_SHIFT);
   page = pt->pages[pt_index];
   return page.present && page.rw;
}

void set_page_rw(pdir_t *pdir, void *vaddrp, bool rw)
{
   page_table_t *pt;
   const ulong vaddr = (ulong) vaddrp;
   const u32 pt_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 pd_index = (vaddr >> BIG_PAGE_SHIFT);

   pt = KERNEL_PA_TO_VA(pdir->entries[pd_index].ptaddr << PAGE_SHIFT);
   ASSERT(KERNEL_VA_TO_PA(pt) != 0);
   pt->pages[pt_index].rw = rw;
   invalidate_page_hw(vaddr);
}

static inline int
__unmap_page(pdir_t *pdir, void *vaddrp, bool free_pageframe, bool permissive)
{
   page_table_t *pt;
   const ulong vaddr = (ulong) vaddrp;
   const u32 pt_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 pd_index = (vaddr >> BIG_PAGE_SHIFT);

   pt = KERNEL_PA_TO_VA(pdir->entries[pd_index].ptaddr << PAGE_SHIFT);

   if (permissive) {

      if (KERNEL_VA_TO_PA(pt) == 0)
         return -EINVAL;

      if (!pt->pages[pt_index].present)
         return -EINVAL;

   } else {
      ASSERT(KERNEL_VA_TO_PA(pt) != 0);
      ASSERT(pt->pages[pt_index].present);
   }

   const ulong paddr = (ulong)
      pt->pages[pt_index].pageAddr << PAGE_SHIFT;

   pt->pages[pt_index].raw = 0;
   invalidate_page_hw(vaddr);

   if (!pf_ref_count_dec(paddr) && free_pageframe) {
      ASSERT(paddr != KERNEL_VA_TO_PA(zero_page));
      kfree2(KERNEL_PA_TO_VA(paddr), PAGE_SIZE);
   }

   return 0;
}

void
unmap_page(pdir_t *pdir, void *vaddrp, bool free_pageframe)
{
   __unmap_page(pdir, vaddrp, free_pageframe, false);
}

int
unmap_page_permissive(pdir_t *pdir, void *vaddrp, bool free_pageframe)
{
   return __unmap_page(pdir, vaddrp, free_pageframe, true);
}

void
unmap_pages(pdir_t *pdir,
            void *vaddr,
            size_t page_count,
            bool do_free)
{
   for (size_t i = 0; i < page_count; i++) {
      unmap_page(pdir, (char *)vaddr + (i << PAGE_SHIFT), do_free);
   }
}

size_t
unmap_pages_permissive(pdir_t *pdir,
                       void *vaddr,
                       size_t page_count,
                       bool do_free)
{
   size_t unmapped_pages = 0;
   int rc;

   for (size_t i = 0; i < page_count; i++) {
      rc = unmap_page_permissive(
         pdir,
         (char *)vaddr + (i << PAGE_SHIFT),
         do_free
      );
      unmapped_pages += (rc == 0);
   }

   return unmapped_pages;
}

ulong get_mapping(pdir_t *pdir, void *vaddrp)
{
   page_table_t *pt;
   const ulong vaddr = (ulong)vaddrp;
   const u32 pt_index = (vaddr >> PAGE_SHIFT) & 0x3FF;
   const u32 pd_index = (vaddr >> BIG_PAGE_SHIFT) & 0x3FF;
   page_dir_entry_t e;
   page_t p;

   /*
    * This function shall be never called for the linear-mapped zone of the
    * the kernel virtual memory.
    */
   ASSERT(vaddr < KERNEL_BASE_VA || vaddr >= LINEAR_MAPPING_END);

   e.raw = pdir->entries[pd_index].raw;
   ASSERT(e.present);
   ASSERT(e.ptaddr != 0);

   pt = KERNEL_PA_TO_VA(e.ptaddr << PAGE_SHIFT);
   p.raw = pt->pages[pt_index].raw;
   ASSERT(p.present);

   return ((ulong) p.pageAddr << PAGE_SHIFT) | (vaddr & OFFSET_IN_PAGE_MASK);
}

int get_mapping2(pdir_t *pdir, void *vaddrp, ulong *pa_ref)
{
   page_table_t *pt;
   const ulong vaddr = (ulong)vaddrp;
   const u32 pt_index = (vaddr >> PAGE_SHIFT) & 0x3FF;
   const u32 pd_index = (vaddr >> BIG_PAGE_SHIFT) & 0x3FF;
   page_dir_entry_t e;
   page_t p;

   /* Get page directory's entry for this vaddr */
   e.raw = pdir->entries[pd_index].raw;

   if (!e.present)
      return -EFAULT;

   if (!e.psize) {

      /* Regular entry, pointing to a page table */

      ASSERT(e.ptaddr != 0);

      /* Get the page table */
      pt = KERNEL_PA_TO_VA(e.ptaddr << PAGE_SHIFT);

      /* Get the page entry for `vaddr` within the page table */
      p.raw = pt->pages[pt_index].raw;

      if (!p.present)
         return -EFAULT;

      *pa_ref = ((ulong) p.pageAddr << PAGE_SHIFT) |
                (vaddr & OFFSET_IN_PAGE_MASK);

   } else {

      /* Big page (4 MB) entry */

      *pa_ref = ((ulong) e.big_4mb_page.paddr << BIG_PAGE_SHIFT) |
                (vaddr & (4 * MB - 1));
   }

   return 0;
}

NODISCARD int
map_page_int(pdir_t *pdir, void *vaddrp, ulong paddr, u32 hw_flags)
{
   page_table_t *pt;
   const u32 vaddr = (u32) vaddrp;
   const u32 pt_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 pd_index = (vaddr >> BIG_PAGE_SHIFT);

   ASSERT(!(vaddr & OFFSET_IN_PAGE_MASK)); // the vaddr must be page-aligned
   ASSERT(!(paddr & OFFSET_IN_PAGE_MASK)); // the paddr must be page-aligned

   pt = KERNEL_PA_TO_VA(pdir->entries[pd_index].ptaddr << PAGE_SHIFT);
   ASSERT(IS_PAGE_ALIGNED(pt));

   if (UNLIKELY(KERNEL_VA_TO_PA(pt) == 0)) {

      // we have to create a page table for mapping 'vaddr'.
      pt = kzalloc_obj(page_table_t);

      if (UNLIKELY(!pt))
         return -ENOMEM;

      ASSERT(IS_PAGE_ALIGNED(pt));

      pdir->entries[pd_index].raw =
         PG_PRESENT_BIT |
         PG_RW_BIT |
         (hw_flags & PG_US_BIT) |
         KERNEL_VA_TO_PA(pt);
   }

   if (pt->pages[pt_index].present)
      return -EADDRINUSE;

   pt->pages[pt_index].raw = PG_PRESENT_BIT | hw_flags | paddr;
   pf_ref_count_inc(paddr);
   invalidate_page_hw(vaddr);
   return 0;
}

NODISCARD size_t
map_pages_int(pdir_t *pdir,
              void *vaddr,
              ulong paddr,
              size_t page_count,
              bool big_pages_allowed,
              u32 hw_flags)
{
   int rc;
   size_t pages = 0;
   size_t big_pages = 0;
   size_t rem_pages = page_count;
   u32 big_page_flags;

   ASSERT(!((ulong)vaddr & OFFSET_IN_PAGE_MASK));
   ASSERT(!(paddr & OFFSET_IN_PAGE_MASK));

   if (big_pages_allowed && rem_pages >= 1024) {

      for (; pages < rem_pages; pages++) {

         if (!((ulong)vaddr & (4*MB - 1)) && !(paddr & (4*MB - 1)))
            break;

         rc = map_page_int(pdir, vaddr, paddr, hw_flags);

         if (UNLIKELY(rc < 0))
            goto out;

         vaddr += PAGE_SIZE;
         paddr += PAGE_SIZE;
      }

      rem_pages -= pages;
      big_page_flags = hw_flags | PG_4MB_BIT | PG_PRESENT_BIT;
      big_page_flags &= ~PG_GLOBAL_BIT;

      for (; big_pages < (rem_pages >> 10); big_pages++) {
         map_4mb_page_int(pdir, vaddr, paddr, big_page_flags);
         vaddr += (4 * MB);
         paddr += (4 * MB);
      }

      rem_pages -= (big_pages << 10);
   }

   for (size_t i = 0; i < rem_pages; i++, pages++) {

      rc = map_page_int(pdir, vaddr, paddr, hw_flags);

      if (UNLIKELY(rc < 0))
         goto out;

      vaddr += PAGE_SIZE;
      paddr += PAGE_SIZE;
   }

out:
   return (big_pages << 10) + pages;
}

NODISCARD int
map_page(pdir_t *pdir, void *vaddrp, ulong paddr, u32 pg_flags)
{
   const bool rw = !!(pg_flags & PAGING_FL_RW);
   const bool us = !!(pg_flags & PAGING_FL_US);
   u32 avail_bits = 0;
   int rc;

   if (pg_flags & PAGING_FL_SHARED)
      avail_bits |= PAGE_SHARED;

   if (pg_flags & PAGING_FL_DO_ALLOC) {

      void *va;
      ASSERT(paddr == 0);

      if (!(va = kmalloc(PAGE_SIZE)))
         return -ENOMEM;

      if (pg_flags & PAGING_FL_ZERO_PG)
         bzero(va, PAGE_SIZE);

      paddr = KERNEL_VA_TO_PA(va);

   } else {

      /* PAGING_FL_ZERO_PG cannot be used without PAGING_FL_DO_ALLOC */
      ASSERT(~pg_flags & PAGING_FL_ZERO_PG);
   }

   rc =
      map_page_int(pdir,
                   vaddrp,
                   paddr,
                   (u32)(avail_bits << PG_CUSTOM_B0_POS) |
                   (u32)(us << PG_US_BIT_POS)            |
                   (u32)(rw << PG_RW_BIT_POS)            |
                   (u32)((!us) << PG_GLOBAL_BIT_POS));
                   /* Kernel pages are global */

   if (UNLIKELY(rc != 0) && (pg_flags & PAGING_FL_DO_ALLOC)) {
      kfree2(KERNEL_PA_TO_VA(paddr), PAGE_SIZE);
   }

   return rc;
}

NODISCARD int
map_zero_page(pdir_t *pdir, void *vaddrp, u32 pg_flags)
{
   u32 avail_bits = 0;
   const bool us = !!(pg_flags & PAGING_FL_US);

   /* Zero pages are always private */
   ASSERT(!(pg_flags & PAGING_FL_SHARED));

   if (pg_flags & PAGING_FL_RW)
      avail_bits |= PAGE_COW_ORIG_RW;

   return
      map_page_int(pdir,
                   vaddrp,
                   KERNEL_VA_TO_PA(&zero_page),
                   (u32)(us << PG_US_BIT_POS) |
                   (u32)(avail_bits << PG_CUSTOM_B0_POS) |
                   (u32)((!us) << PG_GLOBAL_BIT_POS));
                   /* Kernel pages are global */
}

NODISCARD size_t
map_pages(pdir_t *pdir,
          void *vaddr,
          ulong paddr,
          size_t page_count,
          u32 pg_flags)
{
   const bool us = !!(pg_flags & PAGING_FL_US);
   const bool rw = !!(pg_flags & PAGING_FL_RW);
   const bool big_pages = !!(pg_flags & PAGING_FL_BIG_PAGES_ALLOWED);
   u32 avail_bits = 0;

   if (pg_flags & PAGING_FL_SHARED)
      avail_bits |= PAGE_SHARED;

   if (pg_flags & PAGING_FL_DO_ALLOC)
      NOT_IMPLEMENTED();

   return
      map_pages_int(pdir,
                    vaddr,
                    paddr,
                    page_count,
                    big_pages,
                    (u32)(avail_bits << PG_CUSTOM_B0_POS) |
                    (u32)(us << PG_US_BIT_POS)            |
                    (u32)(rw << PG_RW_BIT_POS)            |
                    (u32)((!us) << PG_GLOBAL_BIT_POS));
}

pdir_t *pdir_clone(pdir_t *pdir)
{
   pdir_t *new_pdir = kalloc_obj(pdir_t);

   if (!new_pdir)
      return NULL;

   ASSERT(IS_PAGE_ALIGNED(new_pdir));
   memcpy32(new_pdir, pdir, sizeof(pdir_t) / 4);

   for (u32 i = 0; i < KERNEL_BASE_PD_IDX; i++) {

      /* User-space cannot use 4-MB pages */
      ASSERT(!pdir->entries[i].psize);

      if (!pdir->entries[i].present)
         continue;

      page_table_t *pt = kalloc_obj(page_table_t);

      if (UNLIKELY(!pt)) {

         for (; i > 0; i--) {
            if (pdir->entries[i - 1].present)
               kfree_obj(pdir_get_page_table(pdir, i - 1), page_table_t);
         }

         kfree_obj(new_pdir, pdir_t);
         return NULL;
      }

      ASSERT(IS_PAGE_ALIGNED(pt));
      new_pdir->entries[i].ptaddr=SHR_BITS(KERNEL_VA_TO_PA(pt),PAGE_SHIFT,u32);
   }

   for (u32 i = 0; i < KERNEL_BASE_PD_IDX; i++) {

      if (!pdir->entries[i].present)
         continue;

      page_table_t *orig_pt = pdir_get_page_table(pdir, i);
      page_table_t *new_pt = pdir_get_page_table(new_pdir, i);

      /* Mark all the non-shared pages in that page-table as COW. */
      for (u32 j = 0; j < 1024; j++) {

         page_t *const p = &orig_pt->pages[j];

         if (!p->present)
            continue;

         const ulong orig_paddr = (ulong)p->pageAddr << PAGE_SHIFT;

         /* Sanity-check: a mapped page MUST have ref-count > 0 */
         ASSERT(pf_ref_count_get(orig_paddr) > 0);

         if (!(p->avail & PAGE_SHARED)) {

            if (p->rw)
               p->avail |= PAGE_COW_ORIG_RW;

            p->rw = false;
         }

         pf_ref_count_inc(orig_paddr);
      }

      // copy the page table
      memcpy(new_pt, orig_pt, sizeof(page_table_t));
   }

   return new_pdir;
}

pdir_t *
pdir_deep_clone(pdir_t *pdir)
{
   STATIC_ASSERT(sizeof(pdir_t) == PAGE_SIZE);
   STATIC_ASSERT(sizeof(page_table_t) == PAGE_SIZE);

   struct kmalloc_acc acc;
   kmalloc_create_accelerator(&acc, PAGE_SIZE, 4);

   pdir_t *new_pdir = kmalloc_accelerator_get_elem(&acc);

   if (UNLIKELY(!new_pdir))
      goto oom_exit;

   ASSERT(IS_PAGE_ALIGNED(new_pdir));

   for (u32 i = 0; i < KERNEL_BASE_PD_IDX; i++) {

      new_pdir->entries[i].raw = pdir->entries[i].raw;

      /* User-space cannot use 4-MB pages */
      ASSERT(!pdir->entries[i].psize);

      if (!pdir->entries[i].present)
         continue;

      page_table_t *orig_pt = pdir_get_page_table(pdir, i);
      page_table_t *new_pt = kmalloc_accelerator_get_elem(&acc);

      if (UNLIKELY(!new_pt))
         goto oom_exit;

      ASSERT(IS_PAGE_ALIGNED(new_pt));

      for (u32 j = 0; j < 1024; j++) {

         new_pt->pages[j].raw = orig_pt->pages[j].raw;

         if (!orig_pt->pages[j].present)
            continue;

         void *new_page = kmalloc_accelerator_get_elem(&acc);

         if (!new_page)
            goto oom_exit;

         ASSERT(IS_PAGE_ALIGNED(new_page));

         ulong orig_page_paddr =
            (ulong)orig_pt->pages[j].pageAddr << PAGE_SHIFT;

         void *orig_page = KERNEL_PA_TO_VA(orig_page_paddr);

         u32 new_page_paddr = KERNEL_VA_TO_PA(new_page);
         ASSERT(pf_ref_count_get(new_page_paddr) == 0);
         pf_ref_count_inc(new_page_paddr);

         memcpy32(new_page, orig_page, PAGE_SIZE / 4);
         new_pt->pages[j].pageAddr = SHR_BITS(new_page_paddr, PAGE_SHIFT, u32);
      }

      new_pdir->entries[i].ptaddr =
         SHR_BITS(KERNEL_VA_TO_PA(new_pt), PAGE_SHIFT, u32);
   }

   for (u32 i = KERNEL_BASE_PD_IDX; i < 1024; i++) {
      new_pdir->entries[i].raw = pdir->entries[i].raw;
   }

   kmalloc_destroy_accelerator(&acc);
   return new_pdir;

oom_exit:

   kmalloc_destroy_accelerator(&acc);

   if (new_pdir)
      pdir_destroy(new_pdir);

   return NULL;
}

void pdir_destroy(pdir_t *pdir)
{
   // Kernel's pdir cannot be destroyed!
   ASSERT(pdir != __kernel_pdir);

   for (u32 i = 0; i < KERNEL_BASE_PD_IDX; i++) {

      if (!pdir->entries[i].present)
         continue;

      page_table_t *pt = pdir_get_page_table(pdir, i);

      for (u32 j = 0; j < 1024; j++) {

         if (!pt->pages[j].present)
            continue;

         const ulong paddr = (ulong)pt->pages[j].pageAddr << PAGE_SHIFT;

         if (pf_ref_count_dec(paddr) == 0)
            kfree2(KERNEL_PA_TO_VA(paddr), PAGE_SIZE);
      }

      // We freed all the pages, now free the whole page-table.
      kfree_obj(pt, page_table_t);
   }

   // We freed all pages and all the page-tables, now free pdir.
   kfree_obj(pdir, pdir_t);
}


void map_4mb_page_int(pdir_t *pdir,
                      void *vaddrp,
                      ulong paddr,
                      u32 flags)
{
   const u32 vaddr = (u32) vaddrp;
   const u32 pd_index = (vaddr >> BIG_PAGE_SHIFT);

   ASSERT(!(vaddr & (4*MB - 1))); // the vaddr must be 4MB-aligned
   ASSERT(!(paddr & (4*MB - 1))); // the paddr must be 4MB-aligned

   // Check that the entry has not been used.
   ASSERT(!pdir->entries[pd_index].present);

   // Check that there is no page table associated with this entry.
   ASSERT(!pdir->entries[pd_index].ptaddr);

   pdir->entries[pd_index].raw = flags | paddr;
}

static inline bool in_big_4mb_page(pdir_t *pdir, void *vaddrp)
{
   const u32 vaddr = (u32) vaddrp;
   const u32 pd_index = (vaddr >> BIG_PAGE_SHIFT);

   page_dir_entry_t *e = &pdir->entries[pd_index];
   return e->present && e->psize;
}

static void set_big_4mb_page_pat_wc(pdir_t *pdir, void *vaddrp)
{
   const u32 vaddr = (u32) vaddrp;
   const u32 pd_index = (vaddr >> BIG_PAGE_SHIFT);
   page_dir_entry_t *e = &pdir->entries[pd_index];

   // 111 => entry[7] in the PAT MSR. See init_pat()
   e->big_4mb_page.pat = 1;
   e->big_4mb_page.cd = 1;
   e->big_4mb_page.wt = 1;

   invalidate_page_hw(vaddr);
}

static void set_4kb_page_pat_wc(pdir_t *pdir, void *vaddrp)
{
   page_table_t *pt;
   const u32 vaddr = (u32) vaddrp;
   const u32 pt_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 pd_index = (vaddr >> BIG_PAGE_SHIFT);

   ASSERT(!(vaddr & OFFSET_IN_PAGE_MASK)); // the vaddr must be page-aligned

   pt = KERNEL_PA_TO_VA(pdir->entries[pd_index].ptaddr << PAGE_SHIFT);
   ASSERT(IS_PAGE_ALIGNED(pt));
   ASSERT(pt != NULL);

   // 111 => entry[7] in the PAT MSR. See init_pat()
   pt->pages[pt_index].pat = 1;
   pt->pages[pt_index].cd = 1;
   pt->pages[pt_index].wt = 1;

   invalidate_page_hw(vaddr);
}

void set_pages_pat_wc(pdir_t *pdir, void *vaddr, size_t size)
{
   ASSERT(!((ulong)vaddr & OFFSET_IN_PAGE_MASK));
   ASSERT(IS_PAGE_ALIGNED(size));

   const void *end = vaddr + size;

   while (vaddr < end) {

      if (in_big_4mb_page(pdir, vaddr)) {
         set_big_4mb_page_pat_wc(pdir, vaddr);
         vaddr += 4 * MB;
         continue;
      }

      set_4kb_page_pat_wc(pdir, vaddr);
      vaddr += PAGE_SIZE;
   }
}

void early_init_paging(void)
{
   set_fault_handler(FAULT_PAGE_FAULT, handle_page_fault);
   __kernel_pdir = (pdir_t *) kpdir_buf;
   set_kernel_process_pdir(__kernel_pdir);
}

void init_hi_vmem_heap(void)
{
   size_t hi_vmem_size;
   ulong hi_vmem_start;
   u32 hi_vmem_start_pidx;
   u32 hi_vmem_end_pidx;

   if (LINEAR_MAPPING_MB <= 896) {
      hi_vmem_size = 128 * MB;
   } else {
      panic("LINEAR_MAPPING_MB (%d) is too big", LINEAR_MAPPING_MB);
   }

   hi_vmem_start = LINEAR_MAPPING_END;
   hi_vmem_start_pidx = hi_vmem_start >> BIG_PAGE_SHIFT;
   hi_vmem_end_pidx = hi_vmem_start_pidx + (hi_vmem_size >> BIG_PAGE_SHIFT);

   hi_vmem_heap = kmalloc_create_regular_heap(hi_vmem_start,
                                              hi_vmem_size,
                                              4 * PAGE_SIZE);  // min_block_size

   if (!hi_vmem_heap)
      panic("Failed to create the hi vmem heap");

   for (u32 i = hi_vmem_start_pidx; i < hi_vmem_end_pidx; i++) {

      page_table_t *pt;
      page_dir_entry_t *e = &__kernel_pdir->entries[i];

      ASSERT(!e->present);

      if (!(pt = kzalloc_obj(page_table_t)))
         panic("Unable to alloc ptable for hi_vmem at %p", i << BIG_PAGE_SHIFT);

      ASSERT(IS_PAGE_ALIGNED(pt));
      e->raw = PG_PRESENT_BIT | PG_RW_BIT | PG_US_BIT | KERNEL_VA_TO_PA(pt);
   }
}

void *failsafe_map_framebuffer(ulong paddr, ulong size)
{
   /*
    * Paging has not been initialized yet: probably we're in panic.
    * At this point, the kernel still uses page_size_buf as pdir, with only
    * the first 4 MB of the physical mapped at KERNEL_BASE_VA.
    */

   ulong vaddr = FAILSAFE_FB_VADDR;
   __kernel_pdir = (pdir_t *)page_size_buf;

   u32 big_pages_to_use = pow2_round_up_at(size, 4 * MB) / (4 * MB);

   for (u32 i = 0; i < big_pages_to_use; i++) {
      map_4mb_page_int(__kernel_pdir,
                       (void *)vaddr + i * 4 * MB,
                       paddr + i * 4 * MB,
                       PG_PRESENT_BIT | PG_RW_BIT | PG_4MB_BIT);
   }

   return (void *)vaddr;
}

int
virtual_read_unsafe(pdir_t *pdir, void *extern_va, void *dest, size_t len)
{
   ulong pgoff, pa;
   size_t tot, to_read;
   void *va;

   ASSERT(len <= INT32_MAX);

   for (tot = 0; tot < len; extern_va += to_read, tot += to_read) {

      if (get_mapping2(pdir, extern_va, &pa) < 0)
         return -EFAULT;

      pgoff = ((ulong)extern_va) & OFFSET_IN_PAGE_MASK;
      to_read = MIN(PAGE_SIZE - pgoff, len - tot);

      va = KERNEL_PA_TO_VA(pa);
      memcpy(dest + tot, va, to_read);
   }

   return (int)tot;
}

int
virtual_write_unsafe(pdir_t *pdir, void *extern_va, void *src, size_t len)
{
   ulong pgoff, pa;
   size_t tot, to_write;
   void *va;

   ASSERT(len <= INT32_MAX);

   for (tot = 0; tot < len; extern_va += to_write, tot += to_write) {

      if (get_mapping2(pdir, extern_va, &pa) < 0)
         return -EFAULT;

      pgoff = ((ulong)extern_va) & OFFSET_IN_PAGE_MASK;
      to_write = MIN(PAGE_SIZE - pgoff, len - tot);

      va = KERNEL_PA_TO_VA(pa);
      memcpy(va, src + tot, to_write);
   }

   return (int)tot;
}
