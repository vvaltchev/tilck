/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
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

#include "paging_int.h"

#include <sys/mman.h>      // system header

/*
 * When this flag is set in the 'avail' bits in page_t, in means that the page
 * is writeable even if it marked as read-only and that, on a write attempt
 * the page has to be copied (copy-on-write).
 */
#define PAGE_COW_ORIG_RW 1


/* ---------------------------------------------- */

extern char vsdo_like_page[PAGE_SIZE];
extern char zero_page[PAGE_SIZE] ALIGNED_AT(PAGE_SIZE);

static char kpdir_buf[sizeof(pdir_t)] ALIGNED_AT(PAGE_SIZE);

static u16 *pageframes_refcount;
static uptr phys_mem_lim;
static struct kmalloc_heap *hi_vmem_heap;

static ALWAYS_INLINE u32 pf_ref_count_inc(u32 paddr)
{
   if (paddr >= phys_mem_lim)
      return 0;

   return ++pageframes_refcount[paddr >> PAGE_SHIFT];
}

static ALWAYS_INLINE u32 pf_ref_count_dec(u32 paddr)
{
   if (paddr >= phys_mem_lim)
      return 0;

   ASSERT(pageframes_refcount[paddr >> PAGE_SHIFT] > 0);
   return --pageframes_refcount[paddr >> PAGE_SHIFT];
}

static ALWAYS_INLINE u32 pf_ref_count_get(u32 paddr)
{
   if (paddr >= phys_mem_lim)
      return 0;

   return pageframes_refcount[paddr >> PAGE_SHIFT];
}

static ALWAYS_INLINE page_table_t *
pdir_get_page_table(pdir_t *pdir, u32 i)
{
   return KERNEL_PA_TO_VA(pdir->entries[i].ptaddr << PAGE_SHIFT);
}

void invalidate_page(uptr vaddr)
{
   invalidate_page_hw(vaddr);
}

bool handle_potential_cow(void *context)
{
   regs_t *r = context;
   u32 vaddr;

   if ((r->err_code & PAGE_FAULT_FL_COW) != PAGE_FAULT_FL_COW)
      return false;

   asmVolatile("movl %%cr2, %0" : "=r"(vaddr));

   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));
   void *const page_vaddr = (void *)(vaddr & PAGE_MASK);
   page_table_t *pt = pdir_get_page_table(get_curr_pdir(), page_dir_index);

   if (!(pt->pages[page_table_index].avail & PAGE_COW_ORIG_RW))
      return false; /* Not a COW page */

   const u32 orig_page_paddr = (u32)
      pt->pages[page_table_index].pageAddr << PAGE_SHIFT;

   if (pf_ref_count_get(orig_page_paddr) == 1) {

      /* This page is not shared anymore. No need for copying it. */
      pt->pages[page_table_index].rw = true;
      pt->pages[page_table_index].avail = 0;
      invalidate_page_hw(vaddr);
      return true;
   }

   // Decrease the ref-count of the original pageframe.
   pf_ref_count_dec(orig_page_paddr);

   // Copy the whole page to our temporary buffer.
   memcpy32(page_size_buf, page_vaddr, PAGE_SIZE / 4);

   // Allocate and set a new page.
   void *new_page_vaddr = kmalloc(PAGE_SIZE);

   if (!new_page_vaddr)
      panic("Out-of-memory: unable to copy a CoW page. No OOM killer.");

   ASSERT(IS_PAGE_ALIGNED(new_page_vaddr));

   const uptr paddr = KERNEL_VA_TO_PA(new_page_vaddr);

   /* Sanity-check: a newly allocated pageframe MUST have ref-count == 0 */
   ASSERT(pf_ref_count_get(paddr) == 0);
   pf_ref_count_inc(paddr);

   pt->pages[page_table_index].pageAddr = SHR_BITS(paddr, PAGE_SHIFT, u32);
   pt->pages[page_table_index].rw = true;
   pt->pages[page_table_index].avail = 0;

   invalidate_page_hw(vaddr);

   // Copy back the page.
   memcpy32(page_vaddr, page_size_buf, PAGE_SIZE / 4);
   return true;
}

static void kernel_page_fault_panic(regs_t *r, u32 vaddr, bool rw, bool p)
{
   ptrdiff_t off = 0;
   const char *sym_name = find_sym_at_addr_safe(r->eip, &off, NULL);
   panic("PAGE FAULT in attempt to %s %p from %s%s\nEIP: %p [%s + 0x%x]\n",
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

         if (vfs_handle_fault(um->h, (void *)vaddr, p, rw))
            return;

         sig = SIGBUS;
      }
   }

   printk("USER PAGE FAULT in attempt to %s %p%s\n",
          rw ? "WRITE" : "READ", vaddr,
          !p ? " (NON present)." : ".");

   printk("EIP: %p\n", r->eip);

   end_fault_handler_state();
   send_signal(get_curr_tid(), sig, true);
}


void handle_page_fault(regs_t *r)
{
   if (in_panic()) {

      printk("Page fault while already in panic state.\n");

      while (true) {
         halt();
      }
   }

   ASSERT(!is_preemption_enabled());
   ASSERT(!are_interrupts_enabled());

   enable_interrupts_forced();
   {
      /* Page fault are processed with IF = 1 */
      handle_page_fault_int(r);
   }
   disable_interrupts_forced(); /* restore IF = 0 */
}

bool is_mapped(pdir_t *pdir, void *vaddrp)
{
   page_table_t *pt;
   const uptr vaddr = (uptr) vaddrp;
   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));

   page_dir_entry_t *e = &pdir->entries[page_dir_index];

   if (!e->present)
      return false;

   if (e->psize) /* 4-MB page */
      return e->present;

   pt = KERNEL_PA_TO_VA(pdir->entries[page_dir_index].ptaddr << PAGE_SHIFT);
   return pt->pages[page_table_index].present;
}

void set_page_rw(pdir_t *pdir, void *vaddrp, bool rw)
{
   page_table_t *pt;
   const uptr vaddr = (uptr) vaddrp;
   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));

   pt = KERNEL_PA_TO_VA(pdir->entries[page_dir_index].ptaddr << PAGE_SHIFT);
   ASSERT(KERNEL_VA_TO_PA(pt) != 0);
   pt->pages[page_table_index].rw = rw;
   invalidate_page_hw(vaddr);
}

static inline int
__unmap_page(pdir_t *pdir, void *vaddrp, bool free_pageframe, bool permissive)
{
   page_table_t *pt;
   const uptr vaddr = (uptr) vaddrp;
   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));

   pt = KERNEL_PA_TO_VA(pdir->entries[page_dir_index].ptaddr << PAGE_SHIFT);

   if (permissive) {

      if (KERNEL_VA_TO_PA(pt) == 0)
         return -EINVAL;

      if (!pt->pages[page_table_index].present)
         return -EINVAL;

   } else {
      ASSERT(KERNEL_VA_TO_PA(pt) != 0);
      ASSERT(pt->pages[page_table_index].present);
   }

   const uptr paddr = (uptr)
      pt->pages[page_table_index].pageAddr << PAGE_SHIFT;

   pt->pages[page_table_index].raw = 0;
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

uptr get_mapping(pdir_t *pdir, void *vaddrp)
{
   page_table_t *pt;
   const uptr vaddr = (uptr)vaddrp;
   const u32 pt_index = (vaddr >> PAGE_SHIFT) & 0x3FF;
   const u32 pd_index = (vaddr >> 22) & 0x3FF;
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

   return ((uptr) p.pageAddr << PAGE_SHIFT) | (vaddr & OFFSET_IN_PAGE_MASK);
}

int get_mapping2(pdir_t *pdir, void *vaddrp, uptr *pa_ref)
{
   page_table_t *pt;
   const uptr vaddr = (uptr)vaddrp;
   const u32 pt_index = (vaddr >> PAGE_SHIFT) & 0x3FF;
   const u32 pd_index = (vaddr >> 22) & 0x3FF;
   page_dir_entry_t e;
   page_t p;

   /*
    * This function shall be never called for the linear-mapped zone of the
    * the kernel virtual memory.
    */
   ASSERT(vaddr < KERNEL_BASE_VA || vaddr >= LINEAR_MAPPING_END);

   e.raw = pdir->entries[pd_index].raw;

   if (!e.present)
      return -EFAULT;

   ASSERT(e.ptaddr != 0);
   pt = KERNEL_PA_TO_VA(e.ptaddr << PAGE_SHIFT);
   p.raw = pt->pages[pt_index].raw;

   if (!p.present)
      return -EFAULT;

   *pa_ref = ((uptr) p.pageAddr << PAGE_SHIFT) | (vaddr & OFFSET_IN_PAGE_MASK);
   return 0;
}

NODISCARD int
map_page_int(pdir_t *pdir, void *vaddrp, uptr paddr, u32 flags)
{
   page_table_t *pt;
   const u32 vaddr = (u32) vaddrp;
   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));

   ASSERT(!(vaddr & OFFSET_IN_PAGE_MASK)); // the vaddr must be page-aligned
   ASSERT(!(paddr & OFFSET_IN_PAGE_MASK)); // the paddr must be page-aligned

   pt = KERNEL_PA_TO_VA(pdir->entries[page_dir_index].ptaddr << PAGE_SHIFT);
   ASSERT(IS_PAGE_ALIGNED(pt));

   if (UNLIKELY(KERNEL_VA_TO_PA(pt) == 0)) {

      // we have to create a page table for mapping 'vaddr'.
      pt = kzmalloc(sizeof(page_table_t));

      if (!pt)
         return -ENOMEM;

      ASSERT(IS_PAGE_ALIGNED(pt));

      pdir->entries[page_dir_index].raw =
         PG_PRESENT_BIT |
         PG_RW_BIT |
         (flags & PG_US_BIT) |
         KERNEL_VA_TO_PA(pt);
   }

   if (pt->pages[page_table_index].present)
      return -EADDRINUSE;

   pt->pages[page_table_index].raw = PG_PRESENT_BIT | flags | paddr;
   pf_ref_count_inc(paddr);
   invalidate_page_hw(vaddr);
   return 0;
}

NODISCARD size_t
map_pages_int(pdir_t *pdir,
              void *vaddr,
              uptr paddr,
              size_t page_count,
              bool big_pages_allowed,
              u32 flags)
{
   int rc;
   size_t pages = 0;
   size_t big_pages = 0;
   size_t rem_pages = page_count;
   u32 big_page_flags;

   ASSERT(!((uptr)vaddr & OFFSET_IN_PAGE_MASK));
   ASSERT(!(paddr & OFFSET_IN_PAGE_MASK));

   if (big_pages_allowed && rem_pages >= 1024) {

      for (; pages < rem_pages; pages++) {

         if (!((uptr)vaddr & (4*MB - 1)) && !(paddr & (4*MB - 1)))
            break;

         rc = map_page_int(pdir, vaddr, paddr, flags);

         if (rc < 0)
            goto out;

         vaddr += PAGE_SIZE;
         paddr += PAGE_SIZE;
      }

      rem_pages -= pages;
      big_page_flags = flags | PG_4MB_BIT | PG_PRESENT_BIT;
      big_page_flags &= ~PG_GLOBAL_BIT;

      for (; big_pages < (rem_pages >> 10); big_pages++) {
         map_4mb_page_int(pdir, vaddr, paddr, big_page_flags);
         vaddr += (4 * MB);
         paddr += (4 * MB);
      }

      rem_pages -= (big_pages << 10);
   }

   for (size_t i = 0; i < rem_pages; i++, pages++) {

      rc = map_page_int(pdir, vaddr, paddr, flags);

      if (rc < 0)
         goto out;

      vaddr += PAGE_SIZE;
      paddr += PAGE_SIZE;
   }

out:
   return (big_pages << 10) + pages;
}

NODISCARD int
map_page(pdir_t *pdir,
         void *vaddrp,
         uptr paddr,
         bool us,
         bool rw)
{
   return
      map_page_int(pdir,
                   vaddrp,
                   paddr,
                   (u32)(us << PG_US_BIT_POS) |
                   (u32)(rw << PG_RW_BIT_POS) |
                   (u32)((!us) << PG_GLOBAL_BIT_POS));
                   /* Kernel pages are global */
}

NODISCARD int
map_zero_page(pdir_t *pdir,
              void *vaddrp,
              bool us,
              bool rw)
{
   u32 avail_flags = 0;

   if (rw)
      avail_flags = PAGE_COW_ORIG_RW;

   return
      map_page_int(pdir,
                   vaddrp,
                   KERNEL_VA_TO_PA(&zero_page),
                   (u32)(us << PG_US_BIT_POS) |
                   (u32)(avail_flags << PG_CUSTOM_B0_POS) |
                   (u32)((!us) << PG_GLOBAL_BIT_POS));
                   /* Kernel pages are global */
}

NODISCARD size_t
map_zero_pages(pdir_t *pdir,
               void *vaddrp,
               size_t page_count,
               bool us,
               bool rw)
{
   int rc;
   size_t n;
   uptr vaddr = (uptr) vaddrp;

   for (n = 0; n < page_count; n++, vaddr += PAGE_SIZE) {

      rc = map_zero_page(pdir, (void *)vaddr, us, rw);

      if (rc != 0)
         break;
   }

   return n;
}

NODISCARD size_t
map_pages(pdir_t *pdir,
          void *vaddr,
          uptr paddr,
          size_t page_count,
          bool big_pages_allowed,
          bool us,
          bool rw)
{
   return
      map_pages_int(pdir,
                    vaddr,
                    paddr,
                    page_count,
                    big_pages_allowed,
                    (u32)(us << PG_US_BIT_POS) |
                    (u32)(rw << PG_RW_BIT_POS) |
                    (u32)((!us) << PG_GLOBAL_BIT_POS));
}

pdir_t *pdir_clone(pdir_t *pdir)
{
   pdir_t *new_pdir = kmalloc(sizeof(pdir_t));

   if (!new_pdir)
      return NULL;

   ASSERT(IS_PAGE_ALIGNED(new_pdir));
   memcpy32(new_pdir, pdir, sizeof(pdir_t) / 4);

   for (u32 i = 0; i < (KERNEL_BASE_VA >> 22); i++) {

      /* User-space cannot use 4-MB pages */
      ASSERT(!pdir->entries[i].psize);

      if (!pdir->entries[i].present)
         continue;

      page_table_t *pt = kmalloc(sizeof(page_table_t));

      if (!pt) {

         for (; i > 0; i--) {
            if (pdir->entries[i - 1].present)
               kfree2(pdir_get_page_table(pdir, i - 1), sizeof(page_table_t));
         }

         kfree2(new_pdir, sizeof(pdir_t));
         return NULL;
      }

      ASSERT(IS_PAGE_ALIGNED(pt));
      new_pdir->entries[i].ptaddr=SHR_BITS(KERNEL_VA_TO_PA(pt),PAGE_SHIFT,u32);
   }

   for (u32 i = 0; i < (KERNEL_BASE_VA >> 22); i++) {

      if (!pdir->entries[i].present)
         continue;

      page_table_t *orig_pt = pdir_get_page_table(pdir, i);
      page_table_t *new_pt = pdir_get_page_table(new_pdir, i);

      /* Mark all the pages in that page-table as COW. */
      for (u32 j = 0; j < 1024; j++) {

         if (!orig_pt->pages[j].present)
            continue;

         const uptr orig_paddr = (uptr)orig_pt->pages[j].pageAddr << PAGE_SHIFT;

         /* Sanity-check: a mapped page MUST have ref-count > 0 */
         ASSERT(pf_ref_count_get(orig_paddr) > 0);

         if (orig_pt->pages[j].rw) {
            orig_pt->pages[j].avail |= PAGE_COW_ORIG_RW;
         }

         orig_pt->pages[j].rw = false;
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

   if (!new_pdir)
      goto oom_exit;

   ASSERT(IS_PAGE_ALIGNED(new_pdir));

   for (u32 i = 0; i < (KERNEL_BASE_VA >> 22); i++) {

      new_pdir->entries[i].raw = pdir->entries[i].raw;

      /* User-space cannot use 4-MB pages */
      ASSERT(!pdir->entries[i].psize);

      if (!pdir->entries[i].present)
         continue;

      page_table_t *orig_pt = pdir_get_page_table(pdir, i);
      page_table_t *new_pt = kmalloc_accelerator_get_elem(&acc);

      if (!new_pt)
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

         uptr orig_page_paddr = (uptr)orig_pt->pages[j].pageAddr << PAGE_SHIFT;
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

   for (u32 i = (KERNEL_BASE_VA >> 22); i < 1024; i++) {
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

   for (u32 i = 0; i < (KERNEL_BASE_VA >> 22); i++) {

      if (!pdir->entries[i].present)
         continue;

      page_table_t *pt = pdir_get_page_table(pdir, i);

      for (u32 j = 0; j < 1024; j++) {

         if (!pt->pages[j].present)
            continue;

         const uptr paddr = (uptr)pt->pages[j].pageAddr << PAGE_SHIFT;

         if (pf_ref_count_dec(paddr) == 0)
            kfree2(KERNEL_PA_TO_VA(paddr), PAGE_SIZE);
      }

      // We freed all the pages, now free the whole page-table.
      kfree2(pt, sizeof(*pt));
   }

   // We freed all pages and all the page-tables, now free pdir.
   kfree2(pdir, sizeof(*pdir));
}


void map_4mb_page_int(pdir_t *pdir,
                      void *vaddrp,
                      uptr paddr,
                      u32 flags)
{
   const u32 vaddr = (u32) vaddrp;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));

   ASSERT(!(vaddr & (4*MB - 1))); // the vaddr must be 4MB-aligned
   ASSERT(!(paddr & (4*MB - 1))); // the paddr must be 4MB-aligned

   // Check that the entry has not been used.
   ASSERT(!pdir->entries[page_dir_index].present);

   // Check that there is no page table associated with this entry.
   ASSERT(!pdir->entries[page_dir_index].ptaddr);

   pdir->entries[page_dir_index].raw = flags | paddr;
}

static inline bool in_big_4mb_page(pdir_t *pdir, void *vaddrp)
{
   const u32 vaddr = (u32) vaddrp;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));

   page_dir_entry_t *e = &pdir->entries[page_dir_index];
   return e->present && e->psize;
}

static void set_big_4mb_page_pat_wc(pdir_t *pdir, void *vaddrp)
{
   const u32 vaddr = (u32) vaddrp;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));
   page_dir_entry_t *e = &pdir->entries[page_dir_index];

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
   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));

   ASSERT(!(vaddr & OFFSET_IN_PAGE_MASK)); // the vaddr must be page-aligned

   pt = KERNEL_PA_TO_VA(pdir->entries[page_dir_index].ptaddr << PAGE_SHIFT);
   ASSERT(IS_PAGE_ALIGNED(pt));
   ASSERT(pt != NULL);

   // 111 => entry[7] in the PAT MSR. See init_pat()
   pt->pages[page_table_index].pat = 1;
   pt->pages[page_table_index].cd = 1;
   pt->pages[page_table_index].wt = 1;

   invalidate_page_hw(vaddr);
}

void set_pages_pat_wc(pdir_t *pdir, void *vaddr, size_t size)
{
   ASSERT(!((uptr)vaddr & OFFSET_IN_PAGE_MASK));
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

void init_paging(void)
{
   set_fault_handler(FAULT_PAGE_FAULT, handle_page_fault);
   __kernel_pdir = (pdir_t *) kpdir_buf;
   set_kernel_process_pdir(__kernel_pdir);
}

static void init_hi_vmem_heap(void)
{
   size_t hi_vmem_size;
   size_t metadata_size;
   size_t min_block_size = 4 * PAGE_SIZE;
   uptr hi_vmem_start;
   u32 hi_vmem_end_pdir_index;
   void *metadata;
   bool success;

   hi_vmem_heap = kmalloc(kmalloc_get_heap_struct_size());

   if (!hi_vmem_heap)
      panic("Unable to alloc hi_vmem_heap");

   if (LINEAR_MAPPING_MB <= 896) {
      hi_vmem_size = 128 * MB;
   } else {
      panic("LINEAR_MAPPING_MB (%d) is too big", LINEAR_MAPPING_MB);
   }

   metadata_size = calculate_heap_metadata_size(hi_vmem_size, min_block_size);
   metadata = kmalloc(metadata_size);

   if (!metadata)
      panic("No enough memory for hi vmem heap's metadata");

   hi_vmem_start = LINEAR_MAPPING_END;
   hi_vmem_end_pdir_index = (hi_vmem_start >> 22) + (hi_vmem_size >> 22);

   success =
      kmalloc_create_heap(hi_vmem_heap,
                          hi_vmem_start,
                          hi_vmem_size,
                          min_block_size,
                          0,
                          true,     /* linear mapping true: that's lie! */
                          metadata,
                          NULL,
                          NULL);

   if (!success)
      panic("Failed to create the hi vmem heap");

   for (u32 i = hi_vmem_start >> 22; i < hi_vmem_end_pdir_index; i++) {

      page_table_t *pt;
      page_dir_entry_t *e = &__kernel_pdir->entries[i];

      ASSERT(!e->present);

      if (!(pt = kzmalloc(sizeof(page_table_t))))
         panic("Unable to alloc ptable for hi_vmem at %p", i << 22);

      ASSERT(IS_PAGE_ALIGNED(pt));
      e->raw = PG_PRESENT_BIT | PG_RW_BIT | PG_US_BIT | KERNEL_VA_TO_PA(pt);
   }
}

void init_paging_cow(void)
{
   int rc;
   void *user_vsdo_like_page_vaddr;
   size_t pagesframes_refcount_bufsize;

   phys_mem_lim = get_phys_mem_size();

   /*
    * Allocate the buffer used for keeping a ref-count for each pageframe.
    * This is necessary for COW.
    */

   pagesframes_refcount_bufsize =
      (get_phys_mem_size() >> PAGE_SHIFT) * sizeof(pageframes_refcount[0]);

   pageframes_refcount = kzmalloc(pagesframes_refcount_bufsize);

   if (!pageframes_refcount)
      panic("Unable to allocate pageframes_refcount");

   pf_ref_count_inc(KERNEL_VA_TO_PA(zero_page));

   /* Initialize the kmalloc heap used for the "hi virtual mem" area */
   init_hi_vmem_heap();

   /*
    * Now use the just-created hi vmem heap to reserve a page for the user
    * vsdo-like page and expect it to be == USER_VSDO_LIKE_PAGE_VADDR.
    */
   user_vsdo_like_page_vaddr = hi_vmem_reserve(PAGE_SIZE);

   if (user_vsdo_like_page_vaddr != (void*)USER_VSDO_LIKE_PAGE_VADDR)
      panic("user_vsdo_like_page_vaddr != USER_VSDO_LIKE_PAGE_VADDR");

   /*
    * Map a special vdso-like page used for the sysenter interface.
    * This is the only user-mapped page with a vaddr in the kernel space.
    */
   rc = map_page(__kernel_pdir,
                 user_vsdo_like_page_vaddr,
                 KERNEL_VA_TO_PA(&vsdo_like_page),
                 true,   /* user-visible */
                 false); /* writable */

   if (rc < 0)
      panic("Unable to map the vsdo-like page");
}

static void *failsafe_map_framebuffer(uptr paddr, uptr size)
{
   /*
    * Paging has not been initialized yet: probably we're in panic.
    * At this point, the kernel still uses page_size_buf as pdir, with only
    * the first 4 MB of the physical mapped at KERNEL_BASE_VA.
    */

   uptr vaddr = FAILSAFE_FB_VADDR;
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

void *map_framebuffer(uptr paddr, uptr vaddr, uptr size, bool user_mmap)
{
   if (!get_kernel_pdir())
      return failsafe_map_framebuffer(paddr, size);

   pdir_t *pdir = !user_mmap ? get_kernel_pdir() : get_curr_pdir();
   size_t page_count = pow2_round_up_at(size, PAGE_SIZE) / PAGE_SIZE;
   u32 mmap_flags = PG_RW_BIT | (user_mmap ? PG_US_BIT : PG_GLOBAL_BIT);
   size_t count;

   if (!vaddr) {

      vaddr = (uptr) hi_vmem_reserve(size);

      if (!vaddr) {

         /*
          * This should NEVER happen. The allocation of the hi vmem does not
          * depend at all from the system. It's all on Tilck. We have 128 MB
          * of virtual space that we can allocate as we want. Unless there's
          * a bug in kmalloc(), we'll never get here.
          */

         if (in_panic()) {

            /*
             * But, in the extremely unlucky case we end up here, there's still
             * one thing we can do, at least to be able to show something on
             * the screen: use a failsafe VADDR for the framebuffer.
             */

            vaddr = FAILSAFE_FB_VADDR;

         } else {

            panic("Unable to reserve hi vmem for the framebuffer");
         }
      }
   }

   count = map_pages_int(pdir,
                         (void *)vaddr,
                         paddr,
                         page_count,
                         false, /* big pages not allowed */
                         mmap_flags);

   if (count < page_count) {

      /*
       * What if this is the only framebuffer available for showing something
       * on the screen? Well, we're screwed. But this should *never* happen.
       */

      panic("Unable to map the framebuffer in the virtual space");
   }

   if (x86_cpu_features.edx1.pat) {
      size = pow2_round_up_at(size, PAGE_SIZE);
      set_pages_pat_wc(pdir, (void *) vaddr, size);
      return (void *)vaddr;
   }

   if (!x86_cpu_features.edx1.mtrr || user_mmap)
      return (void *)vaddr;

   /*
    * PAT is not available: we have to use MTRRs in order to make the paddr
    * region be of type WC (write-combining).
    */
   int selected_mtrr = get_free_mtrr();
   uptr pow2size = roundup_next_power_of_2(size);

   if (selected_mtrr < 0) {
      /*
       * Show the error, but still don't fail because the framebuffer can work
       * even without setting its memory region to be WC.
       */
      printk("ERROR: No MTRR available for framebuffer");
      return (void *)vaddr;
   }

   if (pow2_round_up_at(paddr, pow2size) != paddr) {
      /* As above, show the error, but DO NOT fail */
      printk("ERROR: fb_paddr (%p) not aligned at power-of-two address", paddr);
      return (void *)vaddr;
   }

   set_mtrr((u32)selected_mtrr, paddr, pow2size, MEM_TYPE_WC);
   return (void *)vaddr;
}

void *hi_vmem_reserve(size_t size)
{
   void *res;

   disable_preemption();
   {
      res = per_heap_kmalloc(hi_vmem_heap, &size, 0);
   }
   enable_preemption();
   return res;
}

void hi_vmem_release(void *ptr, size_t size)
{
   disable_preemption();
   {
      per_heap_kfree(hi_vmem_heap, ptr, &size, 0);
   }
   enable_preemption();
}

static int
virtual_read_unsafe(pdir_t *pdir, void *extern_va, void *dest, size_t len)
{
   uptr pgoff, pa;
   size_t tot, to_read;
   void *va;

   ASSERT(len <= INT32_MAX);

   for (tot = 0; tot < len; extern_va += to_read, tot += to_read) {

      if (get_mapping2(pdir, extern_va, &pa) < 0)
         return -EFAULT;

      pgoff = ((uptr)extern_va) & OFFSET_IN_PAGE_MASK;
      to_read = MIN(PAGE_SIZE - pgoff, len - tot);

      va = KERNEL_PA_TO_VA(pa);
      memcpy(dest + tot, va, to_read);
   }

   return (int)tot;
}

static int
virtual_write_unsafe(pdir_t *pdir, void *extern_va, void *src, size_t len)
{
   uptr pgoff, pa;
   size_t tot, to_write;
   void *va;

   ASSERT(len <= INT32_MAX);

   for (tot = 0; tot < len; extern_va += to_write, tot += to_write) {

      if (get_mapping2(pdir, extern_va, &pa) < 0)
         return -EFAULT;

      pgoff = ((uptr)extern_va) & OFFSET_IN_PAGE_MASK;
      to_write = MIN(PAGE_SIZE - pgoff, len - tot);

      va = KERNEL_PA_TO_VA(pa);
      memcpy(va, src + tot, to_write);
   }

   return (int)tot;
}

int virtual_read(pdir_t *pdir, void *extern_va, void *dest, size_t len)
{
   int rc;
   disable_preemption();
   {
      rc = virtual_read_unsafe(pdir, extern_va, dest, len);
   }
   enable_preemption();
   return rc;
}

int virtual_write(pdir_t *pdir, void *extern_va, void *src, size_t len)
{
   int rc;
   disable_preemption();
   {
      rc = virtual_write_unsafe(pdir, extern_va, src, len);
   }
   enable_preemption();
   return rc;
}
