/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_mm.h>

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

#include <sys/mman.h>      // system header
#include "paging_int.h"
#include "paging_generic.h"

ulong kernel_va_pa_offset;
ulong linear_va_pa_offset;

pdir_t *__kernel_pdir;
static char kpdir_buf[sizeof(pdir_t)] ALIGNED_AT(PAGE_SIZE);

#define EARLY_PT_NUM 4
static page_table_t early_pt[EARLY_PT_NUM] ALIGNED_AT(PAGE_SIZE);

static page_table_t *alloc_early_pt(void)
{
   static int early_pt_used = 0;

   if (early_pt_used >= EARLY_PT_NUM)
      return NULL;

   return &early_pt[early_pt_used++];
}

static ALWAYS_INLINE page_table_t *
pdir_get_page_table(pdir_t *pdir, ulong vaddr)
{
   page_table_t *pt;
   page_t e;

   for (int level = RV_PAGE_LEVEL; level > 0; level--) {

      e.raw = pdir->entries[PTE_INDEX(level, vaddr)].raw;

      /* Check PTE valid and is not BIG page */
      if (!e.present || (e.raw & _PAGE_LEAF))
         return NULL;

      pt = PA_TO_LIN_VA(e.pfn << PAGE_SHIFT);
      pdir = (pdir_t *)pt;
   }

   return pt;
}

bool handle_potential_cow(void *context)
{
   regs_t *r = context;
   ulong vaddr = r->sbadaddr;
   const void *const page_vaddr = (void *)(vaddr & PAGE_MASK);

   page_table_t *pt = pdir_get_page_table(get_curr_pdir(), vaddr);
   if (!pt)
      return false;

   if (!(pt->entries[PTE_INDEX(0, vaddr)].raw & PAGE_COW_ORIG_RW))
      return false; /* Not a COW page */

   const ulong orig_page_paddr = (ulong)
      pt->entries[PTE_INDEX(0, vaddr)].pfn << PAGE_SHIFT;

   if (pf_ref_count_get(orig_page_paddr) == 1) {

      /* This page is not shared anymore. No need for copying it. */

#if DEBUG_CHECKS
      const ulong paddr = (ulong)
         pt->entries[PTE_INDEX(0, vaddr)].pfn << PAGE_SHIFT;

      ASSERT(paddr != KERNEL_VA_TO_PA(&zero_page));
#endif

      pt->entries[PTE_INDEX(0, vaddr)].wr = true;
      pt->entries[PTE_INDEX(0, vaddr)].reserved = 0;
      invalidate_page_hw(vaddr);
      return true;
   }

   // Allocate a new page.
   void *new_page_vaddr = kmalloc(PAGE_SIZE);

   if (!new_page_vaddr) {

      // Out-of-memory case
      struct task *curr = get_curr_task();

      if (curr->running_in_kernel) {

         // We cannot kill a task running in kernel during a CoW page fault
         // In this case (but in the one above too), Linux puts the process to
         // sleep, while the OOM killer runs and frees some memory.
         panic("Out-of-memory: can't copy a CoW page [pid %d]", get_curr_pid());
      }

      // The task was not running in kernel: we can safely kill it.
      printk("Out-of-memory: killing pid %d\n", get_curr_pid());
      send_signal(get_curr_pid(), SIGKILL, SIG_FL_PROCESS | SIG_FL_FAULT);
      return true;
   }

   ASSERT(IS_L0_PAGE_ALIGNED(new_page_vaddr));

   // Copy page's contents
   memcpy32(new_page_vaddr, page_vaddr, PAGE_SIZE / 4);

   // Get the paddr of the new page
   const ulong paddr = LIN_VA_TO_PA(new_page_vaddr);

   // A just-allocated pageframe MUST have ref-count == 0
   ASSERT(pf_ref_count_get(paddr) == 0);

   // Increase the ref-count of the new pageframe
   pf_ref_count_inc(paddr);

   // Decrease the ref-count of the original pageframe.
   pf_ref_count_dec(orig_page_paddr);

   // Re-map the vaddr to its new (writable) pageframe
   pt->entries[PTE_INDEX(0, vaddr)].pfn = PFN(paddr);
   pt->entries[PTE_INDEX(0, vaddr)].wr = true;
   pt->entries[PTE_INDEX(0, vaddr)].reserved = 0;

   invalidate_page_hw(vaddr);
   return true;
}

static void
kernel_page_fault_panic(regs_t *r, ulong vaddr, bool wr, bool ex, bool rd)
{
   long off = 0;
   const char *sym_name = find_sym_at_addr_safe(r->sepc, &off, NULL);

   panic("PAGE FAULT in attempt to %s %p from %s\nEIP: %p [%s + %d]\n",
         wr ? "WRITE" : ex ? "EXE" : rd ? "READ" : "?",
         vaddr,
         "kernel",
         r->sepc, sym_name ? sym_name : "???", off);
}

void handle_page_fault_int(regs_t *r)
{
   ulong vaddr = r->sbadaddr;
   bool ex = (r->scause == EXC_INST_PAGE_FAULT);
   bool rd = (r->scause == EXC_LOAD_PAGE_FAULT);
   bool wr = (r->scause == EXC_STORE_PAGE_FAULT);
   bool us = !(r->sstatus & SR_SPP);
   bool p = 0;
   int sig = SIGSEGV;
   struct user_mapping *um;
   page_table_t *pt = pdir_get_page_table(get_curr_pdir(), vaddr);

   if (!us) {
      /*
       * Tilck does not support kernel-space page faults caused by the kernel,
       * while it allows user-space page faults caused by kernel (CoW pages).
       * Therefore, such a fault is necessary caused by a bug.
       * We have to panic.
       */
      kernel_page_fault_panic(r, vaddr, wr, ex, rd);
   }

   p = pt ? pt->entries[PTE_INDEX(0, vaddr)].present : 0;
   um = process_get_user_mapping((void *)vaddr);

   if (um) {
      /*
       * Call vfs_handle_fault() only if in first place the mapping allowed
       * writing or if it didn't but the memory access type was a READ.
       */
      if (!!(um->prot & PROT_WRITE) || rd) {

         if (vfs_handle_fault(um, (void *)vaddr, p, wr))
            return;

         sig = SIGBUS;
      }
   }

   if (KRN_PAGE_FAULT_PRINTK) {

      printk("[%d] USER PAGE FAULT in attempt to %s %p%s\n",
             get_curr_pid(),
             wr ? "WRITE" : ex ? "EXE" : rd ? "READ" : "?", TO_PTR(vaddr),
             !p ? " (NON present)." : ".");

      printk("EIP: %p\n", TO_PTR(r->sepc));
      if (get_curr_proc()->debug_cmdline)
         printk("Cmdline: %s\n", get_curr_proc()->debug_cmdline);
   }

   trace_printk(
      5, "USER PAGE FAULT in attempt to %s %p%s, EIP: %p, Cmdline: '%s'",
      wr ? "WRITE" : ex ? "EXE" : rd ? "READ" : "?", TO_PTR(vaddr),
      !p ? " (NON present)" : "",
      TO_PTR(r->sepc),
      get_curr_proc()->debug_cmdline
   );

   send_signal(get_curr_tid(), sig, SIG_FL_PROCESS | SIG_FL_FAULT);
}

bool is_mapped(pdir_t *pdir, void *vaddrp)
{
   page_table_t *pt;
   const ulong vaddr = (ulong) vaddrp;
   page_t *e;

   for (int level = RV_PAGE_LEVEL; level > 0; level--) {

      e = &pdir->entries[PTE_INDEX(level, vaddr)];

      if (!e->present)
         return false;

      if (e->raw & _PAGE_LEAF) /* 2/4-MB page */
         return e->present;

      pt = PA_TO_LIN_VA(e->pfn << PAGE_SHIFT);
      pdir = (pdir_t *)pt;
   }

   return pt->entries[PTE_INDEX(0, vaddr)].present;
}

bool is_rw_mapped(pdir_t *pdir, void *vaddrp)
{
   page_table_t *pt;
   const ulong vaddr = (ulong) vaddrp;
   page_t *e;

   for (int level = RV_PAGE_LEVEL; level > 0; level--) {

      e = &pdir->entries[PTE_INDEX(level, vaddr)];

      if (!e->present)
         return false;

      if (e->raw & _PAGE_LEAF) /* 2/4-MB page */
         return e->present && e->wr;

      pt = PA_TO_LIN_VA(e->pfn << PAGE_SHIFT);
      pdir = (pdir_t *)pt;
   }

   e = &pt->entries[PTE_INDEX(0, vaddr)];
   return e->present && e->wr;
}

void set_page_rw(pdir_t *pdir, void *vaddrp, bool rw)
{
   page_table_t *pt;
   const ulong vaddr = (ulong) vaddrp;

   pt = pdir_get_page_table(pdir, vaddr);
   ASSERT(pt && (LIN_VA_TO_PA(pt) != 0));

   pt->entries[PTE_INDEX(0, vaddr)].wr = rw;
   invalidate_page_hw(vaddr);
}

static inline int
__unmap_page(pdir_t *pdir, void *vaddrp, bool free_pageframe, bool permissive)
{
   page_table_t *pt;
   const ulong vaddr = (ulong) vaddrp;

   pt = pdir_get_page_table(pdir, vaddr);

   if (permissive) {

      if (LIN_VA_TO_PA(pt) == 0)
         return -EINVAL;

      if (!pt->entries[PTE_INDEX(0, vaddr)].present)
         return -EINVAL;

   } else {

      ASSERT(LIN_VA_TO_PA(pt) != 0);
      ASSERT(pt->entries[PTE_INDEX(0, vaddr)].present);
   }

   const ulong paddr = (ulong)
      pt->entries[PTE_INDEX(0, vaddr)].pfn << PAGE_SHIFT;

   pt->entries[PTE_INDEX(0, vaddr)].raw = 0;
   invalidate_page_hw(vaddr);

   if (!pf_ref_count_dec(paddr) && free_pageframe) {

      ASSERT(paddr != KERNEL_VA_TO_PA(zero_page));
      kfree2(PA_TO_LIN_VA(paddr), PAGE_SIZE);
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
   page_t e, p;

   /*
    * This function shall be never called for the linear-mapped zone of the
    * the kernel virtual memory.
    */
   ASSERT(vaddr < BASE_VA || vaddr >= LINEAR_MAPPING_END);

   for (int level = RV_PAGE_LEVEL; level > 0; level--) {

      e.raw = pdir->entries[PTE_INDEX(level, vaddr)].raw;
      ASSERT(e.present);
      ASSERT(e.pfn != 0);
      pdir = PA_TO_LIN_VA(e.pfn << PAGE_SHIFT);
   }

   pt = PA_TO_LIN_VA(e.pfn << PAGE_SHIFT);
   p.raw = pt->entries[PTE_INDEX(0, vaddr)].raw;
   ASSERT(p.present);
   return ((ulong) p.pfn << PAGE_SHIFT) | (vaddr & OFFSET_IN_PAGE_MASK);
}

int get_mapping2(pdir_t *pdir, void *vaddrp, ulong *pa_ref)
{
   page_table_t *pt;
   const ulong vaddr = (ulong)vaddrp;
   page_t p, e;

   for (int level = RV_PAGE_LEVEL; level > 0; level--) {

      e.raw = pdir->entries[PTE_INDEX(level, vaddr)].raw;
      if (!e.present)
         return -EFAULT;

      if (e.raw & _PAGE_LEAF) {
         /* Big page (2/4 MB) entry */
         *pa_ref = ((ulong) e.pfn << PAGE_SHIFT) |
                   (vaddr & (L1_PAGE_SIZE - 1));
         return 0;
      }

      ASSERT(e.pfn != 0);
      pdir = PA_TO_LIN_VA(e.pfn << PAGE_SHIFT);
   }

   /* Get the page table */
   pt = (page_table_t *)pdir;

   /* Get the page entry for `vaddr` within the page table */
   p.raw = pt->entries[PTE_INDEX(0, vaddr)].raw;
   if (!p.present)
         return -EFAULT;

   *pa_ref = ((ulong) p.pfn << PAGE_SHIFT) |
             (vaddr & OFFSET_IN_PAGE_MASK);
   return 0;
}

NODISCARD int
map_page_int(pdir_t *pdir, void *vaddrp, ulong paddr, u32 hw_flags)
{
   page_table_t *pt;
   const ulong vaddr = (ulong) vaddrp;

   ASSERT(IS_L0_PAGE_ALIGNED(vaddr)); // the vaddr must be page-aligned
   ASSERT(IS_L0_PAGE_ALIGNED(paddr)); // the paddr must be page-aligned

   for (int level = RV_PAGE_LEVEL; level > 0; level--) {

      pt = PA_TO_LIN_VA(
         pdir->entries[PTE_INDEX(level, vaddr)].pfn << PAGE_SHIFT
      );

      ASSERT(IS_L0_PAGE_ALIGNED(pt));

      if (UNLIKELY(LIN_VA_TO_PA(pt) == 0)) {

         // we have to create a page table for mapping 'vaddr'.
         pt = kzalloc_obj(page_table_t);

         if (UNLIKELY(!pt))
            return -ENOMEM;

         ASSERT(IS_L0_PAGE_ALIGNED(pt));

         pdir->entries[PTE_INDEX(level, vaddr)].raw =
            PAGE_TABLE | (PFN(LIN_VA_TO_PA(pt)) << _PAGE_PFN_SHIFT);
      }

      pdir = (pdir_t *)pt;
   }

   if (pt->entries[PTE_INDEX(0, vaddr)].present)
      return -EADDRINUSE;

   pt->entries[PTE_INDEX(0, vaddr)].raw =
      _PAGE_PRESENT | hw_flags | (PFN(paddr) << _PAGE_PFN_SHIFT);

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

   ASSERT(IS_L0_PAGE_ALIGNED(vaddr));
   ASSERT(IS_L0_PAGE_ALIGNED(paddr));

   if (big_pages_allowed && rem_pages >= PTRS_PER_PT) {

      for (; pages < rem_pages; pages++) {

         if (IS_L1_PAGE_ALIGNED(vaddr) && IS_L1_PAGE_ALIGNED(paddr))
            break;

         rc = map_page_int(pdir, vaddr, paddr, hw_flags);

         if (UNLIKELY(rc < 0))
            goto out;

         vaddr += PAGE_SIZE;
         paddr += PAGE_SIZE;
      }

      rem_pages -= pages;
      big_page_flags = hw_flags;

      for (; big_pages < (rem_pages / PTRS_PER_PT); big_pages++) {

         map_big_page_int(pdir, vaddr, paddr, big_page_flags);
         vaddr += L1_PAGE_SIZE;
         paddr += L1_PAGE_SIZE;
      }

      rem_pages -= (big_pages * PTRS_PER_PT);
   }

   for (size_t i = 0; i < rem_pages; i++, pages++) {

      rc = map_page_int(pdir, vaddr, paddr, hw_flags);

      if (UNLIKELY(rc < 0))
         goto out;

      vaddr += PAGE_SIZE;
      paddr += PAGE_SIZE;
   }

out:
   return (big_pages * PTRS_PER_PT) + pages;
}

NODISCARD int
map_page(pdir_t *pdir, void *vaddrp, ulong paddr, u32 pg_flags)
{
   const bool rw = !!(pg_flags & PAGING_FL_RW);
   const bool us = !!(pg_flags & PAGING_FL_US);
   u32 avail_bits = 0;
   u32 hw_pg_flags = 0;
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

      paddr = LIN_VA_TO_PA(va);

   } else {

      /* PAGING_FL_ZERO_PG cannot be used without PAGING_FL_DO_ALLOC */
      ASSERT(~pg_flags & PAGING_FL_ZERO_PG);
   }

   hw_pg_flags = _PAGE_BASE |
                 (rw ? _PAGE_WRITE : 0) |
                 (us ? _PAGE_USER : _PAGE_GLOBAL) |
                 avail_bits;

   rc = map_page_int(pdir, vaddrp, paddr, hw_pg_flags);

   if (UNLIKELY(rc != 0) && (pg_flags & PAGING_FL_DO_ALLOC)) {

      kfree2(PA_TO_LIN_VA(paddr), PAGE_SIZE);
   }

   return rc;
}

NODISCARD int
map_zero_page(pdir_t *pdir, void *vaddrp, u32 pg_flags)
{
   u32 avail_bits = 0;
   u32 hw_pg_flags = 0;
   const bool us = !!(pg_flags & PAGING_FL_US);

   /* Zero pages are always private */
   ASSERT(!(pg_flags & PAGING_FL_SHARED));

   if (pg_flags & PAGING_FL_RW)
      avail_bits |= PAGE_COW_ORIG_RW;

   hw_pg_flags = _PAGE_BASE |
                 (us ? _PAGE_USER : _PAGE_GLOBAL) |
                 avail_bits;

   return
      map_page_int(pdir,
                   vaddrp,
                   KERNEL_VA_TO_PA(&zero_page),
                   hw_pg_flags);
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
   u32 hw_pg_flags = 0;

   if (pg_flags & PAGING_FL_SHARED)
      avail_bits |= PAGE_SHARED;

   if (pg_flags & PAGING_FL_DO_ALLOC)
      NOT_IMPLEMENTED();

   hw_pg_flags = _PAGE_BASE |
                 (rw ? _PAGE_WRITE : 0) |
                 (us ? _PAGE_USER : _PAGE_GLOBAL) |
                 avail_bits;

   return
      map_pages_int(pdir,
                    vaddr,
                    paddr,
                    page_count,
                    big_pages,
                    hw_pg_flags);
}

static int
pdir_clone_int(pdir_t *old_pdir,pdir_t *new_pdir,
               u32 pd_idx, u32 level, bool deep)
{
   page_table_t *old_pt, *new_pt;
   int rc;

   if (level == 0) {
      /* Mark all the non-shared pages in that page-table as COW. */
      for (u32 j = 0; j < PTRS_PER_PT; j++) {

         page_t *const e = &old_pdir->entries[j];

         if (!e->present)
            continue;

         const ulong orig_paddr = (ulong)e->pfn << PAGE_SHIFT;

         if (!deep) {

            /* Sanity-check: a mapped page MUST have ref-count > 0 */
            ASSERT(pf_ref_count_get(orig_paddr) > 0);

            if (!(e->raw & PAGE_SHARED)) {

               if (e->wr)
                  e->raw |= PAGE_COW_ORIG_RW;

               e->wr = false;
            }

            pf_ref_count_inc(orig_paddr);

         } else {

            void *new_page = kalloc_obj(page_table_t);

            if (!new_page)
               return -ENOMEM;

            ASSERT(IS_L0_PAGE_ALIGNED(new_page));

            ulong orig_page_paddr =
               (ulong)old_pdir->entries[j].pfn << PAGE_SHIFT;

            void *orig_page = PA_TO_LIN_VA(orig_page_paddr);

            u32 new_page_paddr = LIN_VA_TO_PA(new_page);
            ASSERT(pf_ref_count_get(new_page_paddr) == 0);
            pf_ref_count_inc(new_page_paddr);

            memcpy(new_page, orig_page, PAGE_SIZE);
            new_pdir->entries[j].pfn = PFN(new_page_paddr);
         }
      }

      if (!deep) {
         // copy the page table
         memcpy(new_pdir, old_pdir, sizeof(page_table_t));
      }

      return 0;
   }

   for (u32 i = 0; i < pd_idx; i++) {

      /* User-space cannot use big pages */
      ASSERT (!(old_pdir->entries[i].raw & _PAGE_LEAF));

      if (!old_pdir->entries[i].present)
         continue;

      new_pt = kzalloc_obj(page_table_t);
      old_pt = PA_TO_LIN_VA(old_pdir->entries[i].pfn << PAGE_SHIFT);

      if (UNLIKELY(!new_pt))
         return -ENOMEM;

      ASSERT(IS_L0_PAGE_ALIGNED(new_pt));

      new_pdir->entries[i].pfn = PFN(LIN_VA_TO_PA(new_pt));
      memcpy(new_pt, old_pt, sizeof(page_table_t));

      level--;
      rc = pdir_clone_int(old_pt, new_pt, PTRS_PER_PT, level, deep);
      if (UNLIKELY(rc))
         return -ENOMEM;

      level++;
   }

   return 0;
}

pdir_t *pdir_clone(pdir_t *pdir)
{
   pdir_t *new_pdir = kzalloc_obj(page_table_t);

   if (!new_pdir)
      return NULL;

   ASSERT(IS_L0_PAGE_ALIGNED(new_pdir));
   memcpy32(new_pdir, pdir, sizeof(pdir_t) / 4);

   if (pdir_clone_int(pdir, new_pdir, BASE_VADDR_PD_IDX,
                      RV_PAGE_LEVEL, false))
   {
      pdir_destroy(new_pdir);
      return NULL;
   }

   return new_pdir;
}

static void
pdir_destroy_int(pdir_t *pdir, u32 pd_idx, u32 level)
{
   if (level == 0) {
      for (u32 j = 0; j < PTRS_PER_PT; j++) {

         if (!pdir->entries[j].present)
            continue;

         const ulong paddr = (ulong)pdir->entries[j].pfn << PAGE_SHIFT;

         if (pf_ref_count_dec(paddr) == 0)
            kfree2(PA_TO_LIN_VA(paddr), PAGE_SIZE);
      }

      kfree_obj(pdir, page_table_t);
      return;
   }

   for (u32 i = 0; i < pd_idx; i++) {

      if (!pdir->entries[i].present)
         continue;

      page_table_t *pt = PA_TO_LIN_VA(pdir->entries[i].pfn << PAGE_SHIFT);

      level--;
      pdir_destroy_int((pdir_t *)pt, PTRS_PER_PT, level);
      level++;
   }

   kfree_obj(pdir, page_table_t);
   return;
}

void pdir_destroy(pdir_t *pdir)
{
   // Kernel's pdir cannot be destroyed!
   ASSERT(pdir != __kernel_pdir);

   pdir_destroy_int(pdir, BASE_VADDR_PD_IDX, RV_PAGE_LEVEL);
}

pdir_t *
pdir_deep_clone(pdir_t *pdir)
{
   pdir_t *new_pdir = kzalloc_obj(page_table_t);

   if (!new_pdir)
      return NULL;

   ASSERT(IS_L0_PAGE_ALIGNED(new_pdir));
   memcpy32(new_pdir, pdir, sizeof(pdir_t) / 4);

   if (pdir_clone_int(pdir, new_pdir, BASE_VADDR_PD_IDX,
                      RV_PAGE_LEVEL, true))
   {
      pdir_destroy(new_pdir);
      return NULL;
   }

   return new_pdir;
}

void map_big_page_int(pdir_t *pdir,
                      void *vaddrp,
                      ulong paddr,
                      u32 hw_flags)
{
   page_table_t *pmd;
   const ulong vaddr = (ulong)vaddrp;
   page_t *e;

   ASSERT(IS_L1_PAGE_ALIGNED(vaddr)); // the vaddr must be 2/4MB-aligned
   ASSERT(IS_L1_PAGE_ALIGNED(paddr)); // the paddr must be 2/4MB-aligned

   for (int level = RV_PAGE_LEVEL; level > 1; level--) {

      e = &pdir->entries[PTE_INDEX(level, vaddr)];

      ASSERT(!(e->raw & _PAGE_LEAF)); //super big page, not we want

      pmd = PA_TO_LIN_VA(e->pfn << PAGE_SHIFT);
      ASSERT(IS_L0_PAGE_ALIGNED(pmd));

      if (UNLIKELY(LIN_VA_TO_PA(pmd) == 0)) {

         // we have to create a page table for mapping 'vaddr'.
         pmd = kzalloc_obj(page_table_t);

         if (UNLIKELY(!pmd)) {
            printk("Out-of-memory:map_big_page_int()\n");
            return;
         }

         ASSERT(IS_L0_PAGE_ALIGNED(pmd));
         e->raw = PAGE_TABLE | (PFN(LIN_VA_TO_PA(pmd)) << _PAGE_PFN_SHIFT);
      }

      pdir = pmd;
   }

   // Check that the entry has not been used.
   ASSERT(!pdir->entries[PTE_INDEX(1, vaddr)].present);

   // Check that there is no page table associated with this entry.
   ASSERT(!pdir->entries[PTE_INDEX(1, vaddr)].pfn);

   pdir->entries[PTE_INDEX(1, vaddr)].raw =
      _PAGE_PRESENT | hw_flags | (PFN(paddr) << _PAGE_PFN_SHIFT);

   invalidate_page_hw(vaddr);
}

/*
 * Used in early boot(before kmalloc initialization),
 * only big pages are mapped.
 */
static inline void
create_early_page_table(pdir_t *pdir,
                        ulong vaddr,
                        ulong paddr,
                        size_t lenth,
                        bool in_vm)
{
   ulong end_va = vaddr + lenth;
   ulong pmd_paddr;
   page_t *e;
   page_table_t *tmpdir;

   for (; vaddr < end_va;) {

      tmpdir = pdir;

      for (int level = RV_PAGE_LEVEL; level > 0; level--) {

         e = &tmpdir->entries[PTE_INDEX(level, vaddr)];

         if ((level > 1) && (!e->present)) {

            if (in_vm)
               pmd_paddr = KERNEL_VA_TO_PA((ulong)alloc_early_pt());
            else
               pmd_paddr = (ulong)alloc_early_pt();

            e->raw = (PFN(pmd_paddr) << _PAGE_PFN_SHIFT) | PAGE_TABLE;

            tmpdir = (page_table_t *)pmd_paddr;
         } else {

            tmpdir = (void *)((ulong)e->pfn << PAGE_SHIFT);
         }
      }

      e->raw = MAKE_BIG_PAGE(paddr);

      vaddr += L1_PAGE_SIZE;
      paddr += L1_PAGE_SIZE;
   }
}

void *init_early_mapping(ulong fdt_paddr)
{
   extern char _start;

   pdir_t *pgd = (void *)&page_size_buf[0];
   ulong kernel_pa = (ulong)&_start - 0x1000;
   ulong text_offset = ((struct linux_image_h *)(void *)&_start)->text_offset;
   ulong base_pa = kernel_pa - text_offset;

   kernel_va_pa_offset = KERNEL_BASE_VA - base_pa;
   linear_va_pa_offset = BASE_VA - base_pa;

   /* Copy the flattened device tree to 1MB below kernel's head */
   memcpy((void *)(kernel_pa - MB), (void *)fdt_paddr, MB);

   /* Kernel physical address must be aligned to L1 level big pages(2/4 MB) */
   ASSERT(IS_L1_PAGE_ALIGNED(kernel_pa));

   /* Identity map the first 8 MB */
   create_early_page_table(pgd, base_pa, base_pa, EARLY_MAP_SIZE, false);

   /* Map the first 8 MB also at BASE_VA */
   create_early_page_table(pgd, BASE_VA, base_pa, EARLY_MAP_SIZE, false);

   if (!KRN32_LIN_VADDR) {
      /* Map the first 8 MB AT KERNEL_BASE_VA*/
      create_early_page_table(pgd,
                              KERNEL_BASE_VA,
                              base_pa,
                              EARLY_MAP_SIZE,
                              false);
   }

   /* Flush entire TLB */
   invalidate_page_hw(0);

   /* Return physical addr of FDT */
   return (void *)(kernel_pa - MB);
}

static inline bool
in_big_page(pdir_t *pdir, void *vaddrp)
{
   page_table_t *pmd;
   const ulong vaddr = (ulong) vaddrp;
   page_t *e;

   for (int level = RV_PAGE_LEVEL; level > 1; level--) {

      e = &pdir->entries[PTE_INDEX(level, vaddr)];

      if (!e->present)
         return false;

      /* 1 GB super big page, not we want*/
      if (e->raw & _PAGE_LEAF)
         return false;

      pmd = PA_TO_LIN_VA(e->pfn << PAGE_SHIFT);
      pdir = pmd;
   }

   e = &pdir->entries[PTE_INDEX(1, vaddr)];

   return (e->present) && (e->raw & _PAGE_LEAF);
}

static void
set_big_page_attr(pdir_t *pdir,
                  void *vaddrp,
                  ulong attr)
{
   page_table_t *pmd;
   const ulong vaddr = (ulong) vaddrp;
   ulong *pte;
   page_t *e;

   ASSERT(IS_L1_PAGE_ALIGNED(vaddr)); // the vaddr must be 2/4MB-aligned

   for (int level = RV_PAGE_LEVEL; level > 1; level--) {

      e = &pdir->entries[PTE_INDEX(level, vaddr)];
      ASSERT((e->present) && !(e->raw & _PAGE_LEAF));

      pmd = PA_TO_LIN_VA(e->pfn << PAGE_SHIFT);
      pdir = pmd;
   }

   pte = &pdir->entries[PTE_INDEX(1, vaddr)].raw;
   *pte = (*pte & ~_PAGE_MTMASK ) | attr;
   invalidate_page_hw(vaddr);
}

static void
set_4kb_page_attr(pdir_t *pdir,
                  void *vaddrp,
                  ulong attr)
{
   page_table_t *pt;
   ulong *pte;
   const ulong vaddr = (ulong) vaddrp;

   ASSERT(IS_L0_PAGE_ALIGNED(vaddr)); // the vaddr must be page-aligned

   pt = pdir_get_page_table(pdir, vaddr);
   ASSERT(IS_L0_PAGE_ALIGNED(pt));
   ASSERT(pt != NULL);

   pte = &pt->entries[PTE_INDEX(0, vaddr)].raw;
   *pte = (*pte & ~_PAGE_MTMASK ) | attr;
   invalidate_page_hw(vaddr);
}

void set_pages_pat_wc(pdir_t *pdir, void *vaddr, size_t size)
{
   ASSERT(IS_L0_PAGE_ALIGNED(vaddr));
   ASSERT(IS_L0_PAGE_ALIGNED(size));

   const void *end = vaddr + size;

   while (vaddr < end) {

      if (in_big_page(pdir, vaddr)) {
         set_big_page_attr(pdir, vaddr, _PAGE_WT);
         vaddr += L1_PAGE_SIZE;
         continue;
      }

      set_4kb_page_attr(pdir, vaddr, _PAGE_WT);
      vaddr += PAGE_SIZE;
   }
}

void set_pages_io(pdir_t *pdir, void *vaddr, size_t size)
{
   ASSERT(IS_L0_PAGE_ALIGNED(vaddr));
   ASSERT(IS_L0_PAGE_ALIGNED(size));

   const void *end = vaddr + size;

   while (vaddr < end) {

      if (in_big_page(pdir, vaddr)) {
         set_big_page_attr(pdir, vaddr, _PAGE_IO);
         vaddr += L1_PAGE_SIZE;
         continue;
      }

      set_4kb_page_attr(pdir, vaddr, _PAGE_IO);
      vaddr += PAGE_SIZE;
   }
}

void early_init_paging(void)
{
   set_fault_handler(EXC_INST_PAGE_FAULT, handle_page_fault);
   set_fault_handler(EXC_LOAD_PAGE_FAULT, handle_page_fault);
   set_fault_handler(EXC_STORE_PAGE_FAULT, handle_page_fault);

   __kernel_pdir = PA_TO_LIN_VA(KERNEL_VA_TO_PA(kpdir_buf));
   set_kernel_process_pdir(__kernel_pdir);
   printk("kernel base va:    %p\n", TO_PTR(KERNEL_BASE_VA));
   printk("kernel vaddr:      %p\n", TO_PTR(KERNEL_VADDR));
   printk("base va:           %p\n", TO_PTR(BASE_VA));
   printk("linear mapping:    %lu MB\n", LINEAR_MAPPING_MB);
   printk("\n");

   if (KRN32_LIN_VADDR) {
      /* We're all set up */
      return;
   }

   /*
    * We need to map the kernel's binary into
    * its new pdir.
    */
   create_early_page_table(__kernel_pdir,
                           KERNEL_BASE_VA,
                           KERNEL_VA_TO_PA(KERNEL_BASE_VA),
                           EARLY_MAP_SIZE, true);
}

void init_hi_vmem_heap(void)
{
   size_t pages = 0;
   size_t rem_pages = HI_VMEM_SIZE >> PAGE_SHIFT;
   pdir_t *pdir = __kernel_pdir;
   void *vaddr = (void *)HI_VMEM_START;
   page_table_t *pt;

   if (LINEAR_MAPPING_END > HI_VMEM_START) {
      panic("LINEAR_MAPPING_MB (%d) is too big", LINEAR_MAPPING_MB);
   }

   hi_vmem_heap = kmalloc_create_regular_heap(HI_VMEM_START,
                                              HI_VMEM_SIZE,
                                              4 * PAGE_SIZE);  // min_block_size

   if (!hi_vmem_heap)
      panic("Failed to create the hi vmem heap");

   for (; pages < rem_pages; pages++) {

      for (int level = RV_PAGE_LEVEL; level > 0; level--) {

         pt = PA_TO_LIN_VA(
            pdir->entries[PTE_INDEX(level, vaddr)].pfn << PAGE_SHIFT
         );

         ASSERT(IS_L0_PAGE_ALIGNED(pt));

         if (UNLIKELY(LIN_VA_TO_PA(pt) == 0)) {

            // we have to create a page table for mapping 'vaddr'.
            pt = kzalloc_obj(page_table_t);

            if (UNLIKELY(!pt))
               panic("kmalloc FAIL in init_hi_vmem_heap()\n");

            ASSERT(IS_L0_PAGE_ALIGNED(pt));

            pdir->entries[PTE_INDEX(level, vaddr)].raw =
               PAGE_TABLE | (PFN(LIN_VA_TO_PA(pt)) << _PAGE_PFN_SHIFT);
         }

         pdir = (page_table_t *)pt;
      }

      vaddr += PAGE_SIZE;
      pdir = __kernel_pdir;
   }
}

void *failsafe_map_framebuffer(ulong paddr, ulong size)
{
   /*
    * Paging has not been initialized yet: probably we're in panic.
    * At this point, the kernel still uses page_size_buf as pdir, with only
    * the first 4 MB of the physical mapped at BASE_VA.
    */

   ulong vaddr = FAILSAFE_FB_VADDR;
   __kernel_pdir = PA_TO_LIN_VA(KERNEL_VA_TO_PA(page_size_buf));

   u32 big_pages_to_use = pow2_round_up_at(size, L1_PAGE_SIZE) / L1_PAGE_SIZE;

   for (u32 i = 0; i < big_pages_to_use; i++) {

      map_big_page_int(__kernel_pdir,
                       (void *)vaddr + i * L1_PAGE_SIZE,
                       paddr + i * L1_PAGE_SIZE,
                       _PAGE_BASE | _PAGE_WRITE | _PAGE_GLOBAL);
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

      va = PA_TO_LIN_VA(pa);
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

      va = PA_TO_LIN_VA(pa);
      memcpy(va, src + tot, to_write);
   }

   return (int)tot;
}

