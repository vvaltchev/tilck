
#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/paging.h>
#include <tilck/kernel/irq.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/debug_utils.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/hal.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/elf_utils.h>
#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/errno.h>

#include "paging_int.h"

/*
 * When this flag is set in the 'avail' bits in page_t, in means that the page
 * is writeable even if it marked as read-only and that, on a write attempt
 * the page has to be copied (copy-on-write).
 */
#define PAGE_COW_ORIG_RW 1


/* ---------------------------------------------- */

extern page_directory_t *kernel_page_dir;
extern page_directory_t *curr_page_dir;
extern char page_size_buf[PAGE_SIZE];
extern char vsdo_like_page[PAGE_SIZE];

static char zero_page[PAGE_SIZE] ALIGNED_AT(PAGE_SIZE);

static u16 *pageframes_refcount;
static uptr phys_mem_lim;

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
pdir_get_page_table(page_directory_t *pdir, int i)
{
   return KERNEL_PA_TO_VA(pdir->entries[i].ptaddr << PAGE_SHIFT);
}

bool handle_potential_cow(void *context)
{
   regs *r = context;

   if ((r->err_code & PAGE_FAULT_FL_COW) != PAGE_FAULT_FL_COW)
      return false;

   u32 vaddr;
   asmVolatile("movl %%cr2, %0" : "=r"(vaddr));

   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));
   void *const page_vaddr = (void *)(vaddr & PAGE_MASK);
   page_table_t *ptable = pdir_get_page_table(curr_page_dir, page_dir_index);

   if (!(ptable->pages[page_table_index].avail & PAGE_COW_ORIG_RW))
      return false; /* Not a COW page */

   const u32 orig_page_paddr =
      ptable->pages[page_table_index].pageAddr << PAGE_SHIFT;

   if (pf_ref_count_get(orig_page_paddr) == 1) {

      /* This page is not shared anymore. No need for copying it. */
      ptable->pages[page_table_index].rw = true;
      ptable->pages[page_table_index].avail = 0;
      invalidate_page(vaddr);
      return true;
   }

   // Decrease the ref-count of the original pageframe.
   pf_ref_count_dec(orig_page_paddr);

   // Copy the whole page to our temporary buffer.
   memcpy(page_size_buf, page_vaddr, PAGE_SIZE);

   // Allocate and set a new page.
   void *new_page_vaddr = kmalloc(PAGE_SIZE);

   if (!new_page_vaddr)
      panic("Out-of-memory: unable to copy a CoW page. No OOM killer.");

   ASSERT(IS_PAGE_ALIGNED(new_page_vaddr));

   const uptr paddr = KERNEL_VA_TO_PA(new_page_vaddr);

   /* Sanity-check: a newly allocated pageframe MUST have ref-count == 0 */
   ASSERT(pf_ref_count_get(paddr) == 0);
   pf_ref_count_inc(paddr);

   ptable->pages[page_table_index].pageAddr = paddr >> PAGE_SHIFT;
   ptable->pages[page_table_index].rw = true;
   ptable->pages[page_table_index].avail = 0;

   invalidate_page(vaddr);

   // Copy back the page.
   memcpy(page_vaddr, page_size_buf, PAGE_SIZE);
   return true;
}

void handle_page_fault_int(regs *r)
{
   u32 vaddr;
   asmVolatile("movl %%cr2, %0" : "=r"(vaddr));

   bool p  = !!(r->err_code & PAGE_FAULT_FL_PRESENT);
   bool rw = !!(r->err_code & PAGE_FAULT_FL_RW);
   bool us = !!(r->err_code & PAGE_FAULT_FL_US);

   if (!us) {
      ptrdiff_t off = 0;
      const char *sym_name = find_sym_at_addr_safe(r->eip, &off, NULL);
      panic("PAGE FAULT in attempt to %s %p from %s%s\nEIP: %p [%s + 0x%x]\n",
            rw ? "WRITE" : "READ",
            vaddr,
            "kernel",
            !p ? " (NON present)." : ".",
            r->eip, sym_name ? sym_name : "???", off);
   }

   panic("PAGE FAULT in attempt to %s %p from %s%s\nEIP: %p\n",
         rw ? "WRITE" : "READ",
         vaddr,
         "userland",
         !p ? " (NON present)." : ".", r->eip);

   // We are not really handling yet user page-faults.
}


void handle_page_fault(regs *r)
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


void handle_general_protection_fault(regs *r)
{
   /*
    * For the moment, we don't properly handle GPF yet.
    *
    * TODO: handle GPF caused by user applications with by sending SIGSEGV.
    * Example: user code attempts to execute privileged instructions.
    */
   panic("General protection fault. Error: %p\n", r->err_code);
}

void set_page_directory(page_directory_t *pdir)
{
   curr_page_dir = pdir;
   asmVolatile("mov %0, %%cr3" :: "r"(KERNEL_VA_TO_PA(pdir)));
}

bool is_mapped(page_directory_t *pdir, void *vaddrp)
{
   page_table_t *ptable;
   const uptr vaddr = (uptr) vaddrp;
   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));

   page_dir_entry_t *e = &pdir->entries[page_dir_index];

   if (!e->present)
      return false;

   if (e->psize) /* 4-MB page */
      return e->present;

   ptable = KERNEL_PA_TO_VA(pdir->entries[page_dir_index].ptaddr << PAGE_SHIFT);
   return ptable->pages[page_table_index].present;
}

void set_page_rw(page_directory_t *pdir, void *vaddrp, bool rw)
{
   page_table_t *ptable;
   const uptr vaddr = (uptr) vaddrp;
   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));

   ptable = KERNEL_PA_TO_VA(pdir->entries[page_dir_index].ptaddr << PAGE_SHIFT);
   ASSERT(KERNEL_VA_TO_PA(ptable) != 0);
   ptable->pages[page_table_index].rw = rw;
   invalidate_page(vaddr);
}

void unmap_page(page_directory_t *pdir, void *vaddrp, bool free_pageframe)
{
   page_table_t *ptable;
   const uptr vaddr = (uptr) vaddrp;
   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));

   ptable = KERNEL_PA_TO_VA(pdir->entries[page_dir_index].ptaddr << PAGE_SHIFT);
   ASSERT(KERNEL_VA_TO_PA(ptable) != 0);
   ASSERT(ptable->pages[page_table_index].present);

   const uptr paddr = ptable->pages[page_table_index].pageAddr << PAGE_SHIFT;
   ptable->pages[page_table_index].raw = 0;

   invalidate_page(vaddr);

   if (!pf_ref_count_dec(paddr) && free_pageframe) {
      ASSERT(paddr != KERNEL_VA_TO_PA(zero_page));
      kfree2(KERNEL_PA_TO_VA(paddr), PAGE_SIZE);
   }
}

uptr get_mapping(page_directory_t *pdir, void *vaddrp)
{
   page_table_t *ptable;
   const uptr vaddr = (uptr)vaddrp;
   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 0x3FF;
   const u32 page_dir_index = (vaddr >> 22) & 0x3FF;

   /*
    * This function shall be never called for the linear-mapped zone of the
    * the kernel virtual memory.
    */
   ASSERT(vaddr < KERNEL_BASE_VA || vaddr >= LINEAR_MAPPING_END);

   ptable = KERNEL_PA_TO_VA(pdir->entries[page_dir_index].ptaddr << PAGE_SHIFT);
   ASSERT(KERNEL_VA_TO_PA(ptable) != 0);

   ASSERT(ptable->pages[page_table_index].present);
   return ptable->pages[page_table_index].pageAddr << PAGE_SHIFT;
}

NODISCARD int
map_page_int(page_directory_t *pdir, void *vaddrp, uptr paddr, u32 flags)
{
   page_table_t *ptable;
   const u32 vaddr = (u32) vaddrp;
   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));

   ASSERT(!(vaddr & OFFSET_IN_PAGE_MASK)); // the vaddr must be page-aligned
   ASSERT(!(paddr & OFFSET_IN_PAGE_MASK)); // the paddr must be page-aligned

   ptable = KERNEL_PA_TO_VA(pdir->entries[page_dir_index].ptaddr << PAGE_SHIFT);
   ASSERT(IS_PAGE_ALIGNED(ptable));

   if (UNLIKELY(KERNEL_VA_TO_PA(ptable) == 0)) {

      // we have to create a page table for mapping 'vaddr'.
      ptable = kzmalloc(sizeof(page_table_t));

      if (!ptable)
         return -ENOMEM;

      ASSERT(IS_PAGE_ALIGNED(ptable));

      pdir->entries[page_dir_index].raw =
         PG_PRESENT_BIT |
         PG_RW_BIT |
         (flags & PG_US_BIT) |
         KERNEL_VA_TO_PA(ptable);
   }

   if (ptable->pages[page_table_index].present)
      return -EADDRINUSE;

   ptable->pages[page_table_index].raw = PG_PRESENT_BIT | flags | paddr;
   pf_ref_count_inc(paddr);
   invalidate_page(vaddr);
   return 0;
}

NODISCARD int
map_pages_int(page_directory_t *pdir,
              void *vaddr,
              uptr paddr,
              int page_count,
              bool big_pages_allowed,
              u32 flags)
{
   int rc;
   int pages = 0;
   int big_pages = 0;
   int rem_pages = page_count;
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

   for (int i = 0; i < rem_pages; i++, pages++) {

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
map_page(page_directory_t *pdir,
         void *vaddrp,
         uptr paddr,
         bool us,
         bool rw)
{
   return
      map_page_int(pdir,
                   vaddrp,
                   paddr,
                   (us << PG_US_BIT_POS) |
                   (rw << PG_RW_BIT_POS) |
                   ((!us) << PG_GLOBAL_BIT_POS)); /* Kernel pages are global */
}

NODISCARD int
map_zero_page(page_directory_t *pdir,
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
                   (us << PG_US_BIT_POS) |
                   (avail_flags << PG_CUSTOM_B0_POS) |
                   ((!us) << PG_GLOBAL_BIT_POS)); /* Kernel pages are global */
}

NODISCARD int
map_zero_pages(page_directory_t *pdir,
               void *vaddrp,
               int page_count,
               bool us,
               bool rw)
{
   int n, rc;
   uptr vaddr = (uptr) vaddrp;

   for (n = 0; n < page_count; n++, vaddr += PAGE_SIZE) {

      rc = map_zero_page(pdir, (void *)vaddr, us, rw);

      if (rc != 0)
         break;
   }

   return n;
}

NODISCARD int
map_pages(page_directory_t *pdir,
          void *vaddr,
          uptr paddr,
          int page_count,
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
                    (us << PG_US_BIT_POS) |
                    (rw << PG_RW_BIT_POS) |
                    ((!us) << PG_GLOBAL_BIT_POS));
}

page_directory_t *pdir_clone(page_directory_t *pdir)
{
   page_directory_t *new_pdir = kmalloc(sizeof(page_directory_t));

   if (!new_pdir)
      return NULL;

   ASSERT(IS_PAGE_ALIGNED(new_pdir));
   memcpy32(new_pdir, pdir, sizeof(page_directory_t) / 4);

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

         kfree2(new_pdir, sizeof(page_directory_t));
         return NULL;
      }

      ASSERT(IS_PAGE_ALIGNED(pt));
      new_pdir->entries[i].ptaddr = KERNEL_VA_TO_PA(pt) >> PAGE_SHIFT;
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

         const uptr orig_paddr = orig_pt->pages[j].pageAddr << PAGE_SHIFT;

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

page_directory_t *
pdir_deep_clone(page_directory_t *pdir)
{
   STATIC_ASSERT(sizeof(page_directory_t) == PAGE_SIZE);
   STATIC_ASSERT(sizeof(page_table_t) == PAGE_SIZE);

   kmalloc_accelerator acc;
   kmalloc_create_accelerator(&acc, PAGE_SIZE, 4);

   page_directory_t *new_pdir = kmalloc_accelerator_get_elem(&acc);

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

         uptr orig_page_paddr = orig_pt->pages[j].pageAddr << PAGE_SHIFT;
         void *orig_page = KERNEL_PA_TO_VA(orig_page_paddr);

         u32 new_page_paddr = KERNEL_VA_TO_PA(new_page);
         ASSERT(pf_ref_count_get(new_page_paddr) == 0);
         pf_ref_count_inc(new_page_paddr);

         memcpy32(new_page, orig_page, PAGE_SIZE / 4);
         new_pt->pages[j].pageAddr = new_page_paddr >> PAGE_SHIFT;
      }

      new_pdir->entries[i].ptaddr = KERNEL_VA_TO_PA(new_pt) >> PAGE_SHIFT;
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

void pdir_destroy(page_directory_t *pdir)
{
   // Kernel's pdir cannot be destroyed!
   ASSERT(pdir != kernel_page_dir);

   for (u32 i = 0; i < (KERNEL_BASE_VA >> 22); i++) {

      if (!pdir->entries[i].present)
         continue;

      page_table_t *pt = pdir_get_page_table(pdir, i);

      for (u32 j = 0; j < 1024; j++) {

         if (!pt->pages[j].present)
            continue;

         const u32 paddr = pt->pages[j].pageAddr << PAGE_SHIFT;

         if (pf_ref_count_dec(paddr) == 0)
            kfree2(KERNEL_PA_TO_VA(paddr), PAGE_SIZE);
      }

      // We freed all the pages, now free the whole page-table.
      kfree2(pt, sizeof(*pt));
   }

   // We freed all pages and all the page-tables, now free pdir.
   kfree2(pdir, sizeof(*pdir));
}


void map_4mb_page_int(page_directory_t *pdir,
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

/*
 * Page directories MUST BE page-size-aligned.
 */
static char kpdir_buf[sizeof(page_directory_t)] ALIGNED_AT(PAGE_SIZE);

void init_paging(void)
{
   set_fault_handler(FAULT_PAGE_FAULT, handle_page_fault);
   set_fault_handler(FAULT_GENERAL_PROTECTION, handle_general_protection_fault);
   kernel_page_dir = (page_directory_t *) kpdir_buf;
}

void init_paging_cow(void)
{
   phys_mem_lim = get_phys_mem_size();

   /*
    * Allocate the buffer used for keeping a ref-count for each pageframe.
    * This is necessary for COW.
    */

   size_t pagesframes_refcount_bufsize =
      (get_phys_mem_size() >> PAGE_SHIFT) * sizeof(pageframes_refcount[0]);

   pageframes_refcount = kzmalloc(pagesframes_refcount_bufsize);

   if (!pageframes_refcount)
      panic("Unable to allocate pageframes_refcount");

   pf_ref_count_inc(KERNEL_VA_TO_PA(zero_page));

   /*
    * Map a special vdso-like page used for the sysenter interface.
    * This is the only user-mapped page with a vaddr in the kernel space.
    */
   int rc = map_page(kernel_page_dir,
                     (void *)USER_VSDO_LIKE_PAGE_VADDR,
                     KERNEL_VA_TO_PA(&vsdo_like_page),
                     true,
                     false);

   if (rc < 0)
      panic("Unable to map the vsdo-like page");
}

void map_framebuffer(uptr paddr, uptr vaddr, uptr size)
{
   if (!get_kernel_pdir()) {

      /*
       * Paging has not been initialized yet: probably we're in panic.
       * At this point, the kernel still uses page_size_buf as pdir, with only
       * the first 4 MB of the physical mapped at KERNEL_BASE_VA.
       */

      kernel_page_dir = (page_directory_t *)page_size_buf;

      u32 big_pages_to_use = round_up_at(size, 4 * MB) / (4 * MB);

      for (u32 i = 0; i < big_pages_to_use; i++) {
         map_4mb_page_int(kernel_page_dir,
                          (void *)vaddr + i * 4 * MB,
                          paddr + i * 4 * MB,
                          PG_PRESENT_BIT | PG_RW_BIT | PG_4MB_BIT);
      }

      return;
   }

   int page_count = round_up_at(size, PAGE_SIZE) / PAGE_SIZE;
   int rc;

   rc = map_pages_int(get_kernel_pdir(),
                      (void *)vaddr,
                      paddr,
                      page_count,
                      true, /* big pages allowed */
                      PG_RW_BIT |
                      PG_CD_BIT |
                      PG_GLOBAL_BIT);

   if (rc < page_count)
      panic("Unable to map the framebuffer in the virtual space");

   if (!enable_mttr()) {
      printk("MTRR not available\n");
      return;
   }

   int selected_mtrr = get_free_mtrr();

   if (selected_mtrr < 0) {
      printk("ERROR: No MTRR available for framebuffer");
      return;
   }

   u32 pow2size = roundup_next_power_of_2(size);

   if (round_up_at(paddr, pow2size) != paddr) {
      printk("ERROR: fb_paddr (%p) not aligned at power-of-two address", paddr);
      return;
   }

   set_mtrr(selected_mtrr, paddr, pow2size, MEM_TYPE_WC);
   dump_var_mtrrs();
}
