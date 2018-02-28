
#include <paging.h>
#include <irq.h>
#include <string_util.h>
#include <kmalloc.h>
#include <debug_utils.h>
#include <hal.h>
#include <arch/i386/paging_int.h>

#define PAGE_COW_FLAG 1
#define PAGE_COW_ORIG_RW 2


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

   ptable = curr_page_dir->page_tables[page_dir_index];
   u8 flags = ptable->pages[page_table_index].avail;

   if (!(flags & (PAGE_COW_FLAG | PAGE_COW_ORIG_RW))) {
      // That was not a page-fault caused by COW.
      return false;
   }

   void *page_vaddr = (void *)(vaddr & PAGE_MASK);
   u32 orig_page_paddr = ptable->pages[page_table_index].pageAddr;

   printk("*** DEBUG: attempt to write COW page at %p (addr: %p)\n",
          page_vaddr, vaddr);

   if (pageframes_refcount[orig_page_paddr] == 1) {

      /*
       * This page is not shared anymore. No need for copying it.
       */

      ptable->pages[page_table_index].rw = true;
      ptable->pages[page_table_index].avail = 0;
      invalidate_page(vaddr);

      printk("*** DEBUG: the page was not shared anymore. "
             "Making it writable.\n");

      return true;
   }

   // Decrease the ref-count of the original pageframe.
   pageframes_refcount[orig_page_paddr]--;

   ASSERT(pageframes_refcount[orig_page_paddr] > 0);

   // Copy the whole page to our temporary buffer.
   memmove(page_size_buf, page_vaddr, PAGE_SIZE);

   // Allocate and set a new page.
   void *new_page_vaddr = kmalloc(PAGE_SIZE);
   ASSERT(new_page_vaddr != NULL);
   uptr paddr = KERNEL_VA_TO_PA(new_page_vaddr);

   printk("[COW] Allocated new pageframe at %p\n", paddr);

   ptable->pages[page_table_index].pageAddr = paddr >> PAGE_SHIFT;
   ptable->pages[page_table_index].rw = true;
   ptable->pages[page_table_index].avail = 0;

   invalidate_page(vaddr);

   // Copy back the page.
   memmove(page_vaddr, page_size_buf, PAGE_SIZE);

   // This was actually a COW-caused page-fault.
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

   printk("*** PAGE FAULT in attempt to %s %p from %s%s\n*** EIP: %p\n",
          rw ? "WRITE" : "READ",
          vaddr,
          us ? "userland" : "kernel",
          !p ? " (NON present page)." : ".", r->eip);

   // We are not really handling 'real' page-faults yet.
   NOT_REACHED();
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

static void initialize_empty_page_table(page_table_t *t)
{
   bzero(t, sizeof(page_table_t));
}

bool is_mapped(page_directory_t *pdir, void *vaddrp)
{
   const uptr vaddr = (uptr) vaddrp;
   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));

   if (pdir->page_tables[page_dir_index] == NULL) {
      return false;
   }

   page_table_t *ptable = pdir->page_tables[page_dir_index];
   return ptable->pages[page_table_index].present;
}

void set_page_rw(page_directory_t *pdir, void *vaddrp, bool rw)
{
   const uptr vaddr = (uptr) vaddrp;
   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));

   ASSERT(pdir->page_tables[page_dir_index] != NULL);

   page_table_t *ptable = pdir->page_tables[page_dir_index];
   ptable->pages[page_table_index].rw = rw;

   invalidate_page(vaddr);
}

void unmap_page(page_directory_t *pdir, void *vaddrp)
{
   page_table_t *ptable;
   const uptr vaddr = (uptr) vaddrp;
   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));

   ASSERT(pdir->page_tables[page_dir_index] != NULL);

   ptable = pdir->page_tables[page_dir_index];

   ASSERT(ptable->pages[page_table_index].present);

   page_t p = {0};
   ptable->pages[page_table_index] = p;

   invalidate_page(vaddr);
}

uptr get_mapping(page_directory_t *pdir, void *vaddrp)
{
   page_table_t *ptable;
   const uptr vaddr = (uptr)vaddrp;
   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 0x3FF;
   const u32 page_dir_index = (vaddr >> 22) & 0x3FF;

   ptable = pdir->page_tables[page_dir_index];

   ASSERT(ptable != NULL);
   ASSERT(ptable->pages[page_table_index].present);
   return ptable->pages[page_table_index].pageAddr << PAGE_SHIFT;
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
   ASSERT(!pdir->page_tables[page_dir_index]);

   pdir->entries[page_dir_index].raw = flags | paddr;
}

void map_page_int(page_directory_t *pdir,
                  void *vaddrp,
                  uptr paddr,
                  u32 flags)
{
   const u32 vaddr = (u32) vaddrp;
   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));

   ASSERT(!(vaddr & OFFSET_IN_PAGE_MASK)); // the vaddr must be page-aligned
   ASSERT(!(paddr & OFFSET_IN_PAGE_MASK)); // the paddr must be page-aligned

   page_table_t *ptable = pdir->page_tables[page_dir_index];

   ASSERT(((uptr)ptable & OFFSET_IN_PAGE_MASK) == 0);

   if (UNLIKELY(ptable == NULL)) {

      // we have to create a page table for mapping 'vaddr'.
      void *buf = kmalloc(sizeof(page_table_t));

      // Don't handle this type of out-of-memory for the moment.
      VERIFY(buf != NULL);

      ptable = buf;

      initialize_empty_page_table(ptable);
      pdir->page_tables[page_dir_index] = ptable;

      pdir->entries[page_dir_index].raw =
         PG_PRESENT_BIT |
         PG_RW_BIT |
         (flags & PG_US_BIT) |
         KERNEL_VA_TO_PA(ptable);
   }

   ASSERT(ptable->pages[page_table_index].present == 0);

   ptable->pages[page_table_index].raw = PG_PRESENT_BIT | flags | paddr;
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
   memmove(new_pdir, pdir, sizeof(page_directory_t));

   for (int i = 0; i < 1024; i++) {

      if (!pdir->page_tables[i])
         continue;

      page_table_t *orig_pt = pdir->page_tables[i];

      /*
       * Mark all the pages in that page-table as COW.
       */
      for (int j = 0; j < 1024; j++) {

         if (orig_pt->pages[j].avail & PAGE_COW_FLAG) {
            // The page is already COW. Just increase its ref-count.
            pageframes_refcount[orig_pt->pages[j].pageAddr]++;
            continue;
         }

         int flags = PAGE_COW_FLAG;

         if (orig_pt->pages[j].rw) {
            flags |= PAGE_COW_ORIG_RW;
         }

         orig_pt->pages[j].avail = flags;
         orig_pt->pages[j].rw = false;

         // We're making for the first time this page to be COW.
         pageframes_refcount[orig_pt->pages[j].pageAddr] = 2;
      }


      // alloc memory for the page table

      page_table_t *pt = kmalloc(sizeof(*pt));
      ASSERT(((uptr)pt & (PAGE_SIZE - 1)) == 0); // pt must be page-aligned
      VERIFY(pt); // Don't handle this kind of out-of-memory for the moment.

      // copy the page table
      memmove(pt, orig_pt, sizeof(*pt));

      new_pdir->page_tables[i] = pt;

      /* We've already copied the other members of new_pdir->entries[i] */
      new_pdir->entries[i].pageTableAddr = KERNEL_VA_TO_PA(pt) >> PAGE_SHIFT;
   }

   return new_pdir;
}


void pdir_destroy(page_directory_t *pdir)
{
   // Kernel's pdir cannot be destroyed!
   ASSERT(pdir != kernel_page_dir);

   // Assumption: [0, 768) because KERNEL_BASE_VA is 0xC0000000.
   for (int i = 0; i < 768; i++) {

      page_table_t *pt = pdir->page_tables[i];

      if (!pt)
         continue;

      for (int j = 0; j < 1024; j++) {

         if (!pt->pages[j].present)
            continue;

         const u32 paddr = pt->pages[j].pageAddr;

         if (pt->pages[j].avail & PAGE_COW_FLAG) {

            ASSERT(pageframes_refcount[paddr] > 0);

            if (pageframes_refcount[paddr] > 1) {
               pageframes_refcount[paddr]--;
               continue;
            }
         }

         // No COW (or COW with ref-count == 1).
         kfree(KERNEL_PA_TO_VA(paddr << PAGE_SHIFT), PAGE_SIZE);
      }

      // We freed all the pages, now free the whole page-table.
      kfree(pt, sizeof(*pt));
   }

   // We freed all pages and all the page-tables, now free pdir.
   kfree(pdir, sizeof(*pdir));
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
                       PG_PRESENT_BIT | PG_RW_BIT | PG_4MB_BIT | paddr);
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
