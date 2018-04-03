
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/paging.h>
#include <exos/irq.h>
#include <exos/kmalloc.h>
#include <exos/debug_utils.h>
#include <exos/process.h>
#include <exos/hal.h>

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
extern u16 *pageframes_refcount;
extern u8 page_size_buf[PAGE_SIZE];

bool handle_potential_cow(u32 vaddr)
{
   page_table_t *ptable;
   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));

   ptable = KERNEL_PA_TO_VA(curr_page_dir->entries[page_dir_index].ptaddr<<12);

   if (!(ptable->pages[page_table_index].avail & PAGE_COW_ORIG_RW))
      return false; /* Not a COW page */

   void *page_vaddr = (void *)(vaddr & PAGE_MASK);
   u32 orig_page_paddr = ptable->pages[page_table_index].pageAddr;

   if (pageframes_refcount[orig_page_paddr] == 1) {

      /* This page is not shared anymore. No need for copying it. */
      ptable->pages[page_table_index].rw = true;
      ptable->pages[page_table_index].avail = 0;
      invalidate_page(vaddr);
      return true;
   }

   // Decrease the ref-count of the original pageframe.
   pageframes_refcount[orig_page_paddr]--;

   // Copy the whole page to our temporary buffer.
   memcpy(page_size_buf, page_vaddr, PAGE_SIZE);

   // Allocate and set a new page.
   void *new_page_vaddr = kmalloc(PAGE_SIZE);
   VERIFY(new_page_vaddr != NULL); // Don't handle this out-of-mem for now.
   ASSERT(PAGE_ALIGNED(new_page_vaddr));

   uptr paddr = KERNEL_VA_TO_PA(new_page_vaddr);
   ASSERT(pageframes_refcount[paddr >> PAGE_SHIFT] == 0);
   pageframes_refcount[paddr >> PAGE_SHIFT]++;

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

   bool us = (r->err_code & (1 << 2)) != 0;
   bool rw = (r->err_code & (1 << 1)) != 0;
   bool p = (r->err_code & (1 << 0)) != 0;

   if (us && rw && p && handle_potential_cow(vaddr)) {
      return;
   }

   if (!us) {
      ptrdiff_t off = 0;
      const char *sym_name = find_sym_at_addr(r->eip, &off);
      panic("PAGE FAULT in attempt to %s %p from %s%s\nEIP: %p [%s + 0x%x]\n",
            rw ? "WRITE" : "READ",
            vaddr,
            "kernel",
            !p ? " (NON present page)." : ".",
            r->eip, sym_name ? sym_name : "???", off);
   }

   panic("PAGE FAULT in attempt to %s %p from %s%s\nEIP: %p\n",
         rw ? "WRITE" : "READ",
         vaddr,
         "userland",
         !p ? " (NON present page)." : ".", r->eip);

   // We are not really handling 'real' page-faults yet.
}

extern volatile bool in_panic;
DEBUG_ONLY(volatile bool in_page_fault = false);

void handle_page_fault(regs *r)
{
   ASSERT(!in_page_fault);
   DEBUG_ONLY(in_page_fault = true);
   ASSERT(!is_preemption_enabled());

   if (!in_panic) {
      handle_page_fault_int(r);
   }

   DEBUG_ONLY(in_page_fault = false);
}

void handle_general_protection_fault(regs *r)
{
   disable_interrupts_forced();
   printk("General protection fault. Error: %p\n", r->err_code);
   halt();
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

   if (e->psize)
      return true; /* 4-MB page */

   ptable = KERNEL_PA_TO_VA(pdir->entries[page_dir_index].ptaddr << 12);
   return ptable->pages[page_table_index].present;
}

void set_page_rw(page_directory_t *pdir, void *vaddrp, bool rw)
{
   page_table_t *ptable;
   const uptr vaddr = (uptr) vaddrp;
   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));

   ptable = KERNEL_PA_TO_VA(pdir->entries[page_dir_index].ptaddr << 12);
   ASSERT(KERNEL_VA_TO_PA(ptable) != 0);
   ptable->pages[page_table_index].rw = rw;
   invalidate_page(vaddr);
}

void unmap_page(page_directory_t *pdir, void *vaddrp)
{
   page_table_t *ptable;
   const uptr vaddr = (uptr) vaddrp;
   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));

   ptable = KERNEL_PA_TO_VA(pdir->entries[page_dir_index].ptaddr << PAGE_SHIFT);
   ASSERT(KERNEL_VA_TO_PA(ptable) != 0);
   ASSERT(ptable->pages[page_table_index].present);
   ptable->pages[page_table_index].raw = 0;

   uptr shifted_paddr = ptable->pages[page_table_index].pageAddr;
   ASSERT(pageframes_refcount[shifted_paddr] > 0);

   if (--pageframes_refcount[shifted_paddr] == 0) {
      kfree2(KERNEL_PA_TO_VA(shifted_paddr << PAGE_SHIFT), PAGE_SIZE);
   }

   invalidate_page(vaddr);
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
   ASSERT(vaddr < KERNEL_BASE_VA || vaddr >= LINEAR_MAPPING_OVER_END);

   ptable = KERNEL_PA_TO_VA(pdir->entries[page_dir_index].ptaddr << 12);
   ASSERT(KERNEL_VA_TO_PA(ptable) != 0);

   ASSERT(ptable->pages[page_table_index].present);
   return ptable->pages[page_table_index].pageAddr << PAGE_SHIFT;
}

void map_page_int(page_directory_t *pdir,
                  void *vaddrp,
                  uptr paddr,
                  u32 flags)
{
   page_table_t *ptable;
   const u32 vaddr = (u32) vaddrp;
   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));

   ASSERT(!(vaddr & OFFSET_IN_PAGE_MASK)); // the vaddr must be page-aligned
   ASSERT(!(paddr & OFFSET_IN_PAGE_MASK)); // the paddr must be page-aligned

   ptable = KERNEL_PA_TO_VA(pdir->entries[page_dir_index].ptaddr << 12);
   ASSERT(PAGE_ALIGNED(ptable));

   if (UNLIKELY(KERNEL_VA_TO_PA(ptable) == 0)) {

      // we have to create a page table for mapping 'vaddr'.
      ptable = kzmalloc(sizeof(page_table_t));
      VERIFY(ptable != NULL); // Don't handle this out-of-memory for now.
      ASSERT(PAGE_ALIGNED(ptable));

      pdir->entries[page_dir_index].raw =
         PG_PRESENT_BIT |
         PG_RW_BIT |
         (flags & PG_US_BIT) |
         KERNEL_VA_TO_PA(ptable);
   }

   ASSERT(ptable->pages[page_table_index].present == 0);

   ptable->pages[page_table_index].raw = PG_PRESENT_BIT | flags | paddr;
   pageframes_refcount[ptable->pages[page_table_index].pageAddr]++;
   invalidate_page(vaddr);
}

void map_page(page_directory_t *pdir,
              void *vaddrp,
              uptr paddr,
              bool us,
              bool rw)
{
   map_page_int(pdir,
                vaddrp,
                paddr,
                (us << PG_US_BIT_POS) |
                (rw << PG_RW_BIT_POS) |
                ((!us) << PG_GLOBAL_BIT_POS)); /* Kernel pages are 'global' */
}

static inline void
map_pages_int(page_directory_t *pdir,
              void *vaddr,
              uptr paddr,
              int page_count,
              u32 flags)
{
   for (int i = 0; i < page_count; i++) {
      map_page_int(pdir,
                   (u8 *)vaddr + (i << PAGE_SHIFT),
                   paddr + (i << PAGE_SHIFT),
                   flags);
   }
}

page_directory_t *pdir_clone(page_directory_t *pdir)
{
   page_directory_t *new_pdir = kmalloc(sizeof(page_directory_t));

   if (!new_pdir)
      return NULL;

   memcpy(new_pdir, pdir, sizeof(page_directory_t));

   for (int i = 0; i < 1024; i++) {

      /* 4-MB pages don't have page tables, so we already copied them. */
      if (pdir->entries[i].psize)
         continue;

      if (!pdir->entries[i].present)
         continue;

      page_table_t *orig_pt = KERNEL_PA_TO_VA(pdir->entries[i].ptaddr << 12);

      /*
       * Mark all the pages in that page-table as COW.
       */
      for (int j = 0; j < 1024; j++) {

         if (!orig_pt->pages[j].present)
            continue;

         /* Sanity-check: a mapped page MUST have ref-count > 0 */
         ASSERT(pageframes_refcount[orig_pt->pages[j].pageAddr] > 0);

         if (orig_pt->pages[j].rw) {
            orig_pt->pages[j].avail |= PAGE_COW_ORIG_RW;
         }

         orig_pt->pages[j].rw = false;

         // We're making for the first time this page to be COW.
         pageframes_refcount[orig_pt->pages[j].pageAddr]++;
      }

      // alloc memory for the page table

      page_table_t *pt = kmalloc(sizeof(*pt));
      VERIFY(pt); // Don't handle this kind of out-of-memory for the moment.
      ASSERT(PAGE_ALIGNED(pt));

      // copy the page table
      memcpy(pt, orig_pt, sizeof(*pt));

      /* We've already copied the other members of new_pdir->entries[i] */
      new_pdir->entries[i].ptaddr = KERNEL_VA_TO_PA(pt) >> PAGE_SHIFT;
   }

   return new_pdir;
}


void pdir_destroy(page_directory_t *pdir)
{
   // Kernel's pdir cannot be destroyed!
   ASSERT(pdir != kernel_page_dir);

   // Assumption: [0, 768) because KERNEL_BASE_VA is 0xC0000000.
   for (int i = 0; i < 768; i++) {

      if (!pdir->entries[i].present)
         continue;

      page_table_t *pt = KERNEL_PA_TO_VA(pdir->entries[i].ptaddr << 12);

      for (int j = 0; j < 1024; j++) {

         if (!pt->pages[j].present)
            continue;

         const u32 paddr = pt->pages[j].pageAddr;

         if (--pageframes_refcount[paddr] == 0)
            kfree2(KERNEL_PA_TO_VA(paddr << PAGE_SHIFT), PAGE_SIZE);
      }

      // We freed all the pages, now free the whole page-table.
      kfree2(pt, sizeof(*pt));
   }

   // We freed all pages and all the page-tables, now free pdir.
   kfree2(pdir, sizeof(*pdir));
}


static void map_4mb_page_int(page_directory_t *pdir,
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
char kpdir_buf[sizeof(page_directory_t)] __attribute__ ((aligned(PAGE_SIZE)));

void init_paging()
{
   set_fault_handler(FAULT_PAGE_FAULT, handle_page_fault);
   set_fault_handler(FAULT_GENERAL_PROTECTION, handle_general_protection_fault);
   kernel_page_dir = (page_directory_t *) kpdir_buf;

   /*
    * Linear mapping: map the first LINEAR_MAPPING_MB of the physical
    * memory in the virtual memory with offset KERNEL_BASE_VA.
    */

   for (uptr paddr = 0; paddr < LINEAR_MAPPING_SIZE; paddr += 4 * MB) {

      if (paddr >= get_phys_mem_size())
         break;

      map_4mb_page_int(kernel_page_dir,
                       KERNEL_PA_TO_VA(paddr),
                       paddr,
                       PG_PRESENT_BIT | PG_RW_BIT | PG_4MB_BIT);
   }

   set_page_directory(kernel_page_dir);
}

void init_paging_cow(void)
{
   /*
    * Allocate the buffer used for keeping a ref-count for each pageframe.
    * This is necessary for COW.
    */

   size_t pagesframes_refcount_bufsize =
      sizeof(pageframes_refcount[0]) * MB / PAGE_SIZE * get_phys_mem_mb();

   pageframes_refcount = kzmalloc(pagesframes_refcount_bufsize);
   VERIFY(pageframes_refcount);
}
