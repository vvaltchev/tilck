
#include <arch/i386/arch_utils.h>
#include <paging.h>
#include <irq.h>
#include <string_util.h>
#include <kmalloc.h>
#include <debug_utils.h>

#include <arch/i386/paging_int.h>

/*
 * Theese MACROs can be used only for the first 4 MB of the kernel virtual
 * address space.
 */

#define KERNEL_PA_TO_VA(pa) ((typeof(pa))((uptr)(pa) + KERNEL_BASE_VA))
#define KERNEL_VA_TO_PA(va) ((typeof(va))((uptr)(va) - KERNEL_BASE_VA))

uptr paging_alloc_pageframe();
void paging_free_pageframe(uptr address);

#ifdef DEBUG
bool is_allocated_pageframe(void *address);
#endif

#define PAGE_COW_FLAG 1
#define PAGE_COW_ORIG_RW 2


/* ---------------------------------------------- */

page_directory_t *kernel_page_dir = NULL;
page_directory_t *curr_page_dir = NULL;
void *page_size_buf = NULL;
u16 *pageframes_refcount = NULL;

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
   uptr paddr = (uptr)alloc_pageframe();

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

extern volatile bool in_panic;

void handle_page_fault(regs *r)
{
   u32 vaddr;

   if (in_panic) {
      return;
   }

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

void handle_general_protection_fault(regs *r)
{
   disable_interrupts();
   printk("General protection fault. Error: %p\n", r->err_code);
   halt();
}

void set_page_directory(page_directory_t *pdir)
{
   curr_page_dir = pdir;
   asmVolatile("mov %0, %%cr3" :: "r"(pdir->paddr));
}

static void initialize_empty_page_table(page_table_t *t)
{
   for (int i = 0; i < 1024; i++) {
      t->pages[i].present = 0;
      t->pages[i].pageAddr = 0;
   }
}

void initialize_page_directory(page_directory_t *pdir, uptr paddr, bool us)
{
   page_dir_entry_t not_present = {0};

   not_present.present = 0;
   not_present.rw = 1;
   not_present.us = us;
   not_present.pageTableAddr = 0;

   pdir->paddr = paddr;

   for (int i = 0; i < 1024; i++) {
      pdir->entries[i] = not_present;
      pdir->page_tables[i] = NULL;
   }
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
   uptr vaddr = (uptr)vaddrp;
   page_table_t *ptable;
   u32 page_table_index = (vaddr >> PAGE_SHIFT) & 0x3FF;
   u32 page_dir_index = (vaddr >> 22) & 0x3FF;

   ASSERT(pdir->page_tables[page_dir_index] != NULL);

   ptable = pdir->page_tables[page_dir_index];

   ASSERT(ptable->pages[page_table_index].present);

   return ptable->pages[page_table_index].pageAddr << PAGE_SHIFT;
}

void map_page(page_directory_t *pdir,
              void *vaddrp,
              uptr paddr,
              bool us,
              bool rw)
{
   const u32 vaddr = (u32) vaddrp;
   const u32 page_table_index = (vaddr >> PAGE_SHIFT) & 1023;
   const u32 page_dir_index = (vaddr >> (PAGE_SHIFT + 10));

   ASSERT(!(vaddr & OFFSET_IN_PAGE_MASK)); // the vaddr must be page-aligned
   ASSERT(!(paddr & OFFSET_IN_PAGE_MASK)); // the paddr must be page-aligned

   page_table_t *ptable = pdir->page_tables[page_dir_index];

   ASSERT(((uptr)ptable & OFFSET_IN_PAGE_MASK) == 0);

   if (UNLIKELY(ptable == NULL)) {

      // we have to create a page table for mapping 'vaddr'

      u32 page_physical_addr = (u32)paging_alloc_pageframe();

      ptable = (void*)KERNEL_PA_TO_VA(page_physical_addr);

      initialize_empty_page_table(ptable);

      page_dir_entry_t e = {0};
      e.present = 1;
      e.rw = 1;
      e.us = us;
      e.pageTableAddr = ((u32)page_physical_addr) >> PAGE_SHIFT;

      pdir->page_tables[page_dir_index] = ptable;
      pdir->entries[page_dir_index] = e;
   }

   ASSERT(ptable->pages[page_table_index].present == 0);

   page_t p = {0};
   p.present = 1;
   p.us = us;
   p.rw = rw;
   p.global = !us; /* All kernel pages are 'global'. */
   p.pageAddr = paddr >> PAGE_SHIFT;

   ptable->pages[page_table_index] = p;
   invalidate_page(vaddr);
}

page_directory_t *pdir_clone(page_directory_t *pdir)
{
   page_directory_t *new_pdir = kmalloc(sizeof(page_directory_t));
   new_pdir->paddr = get_mapping(curr_page_dir, new_pdir);

   page_dir_entry_t not_present = { 0 };

   not_present.present = 0;
   not_present.rw = 1;
   not_present.us = true;
   not_present.pageTableAddr = 0;

   for (int i = 0; i < 768; i++) {

      if (pdir->page_tables[i] == NULL) {
         new_pdir->entries[i] = not_present;
         new_pdir->page_tables[i] = NULL;
         continue;
      }

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

         /*
          * If we wanted to use invalidate_page() instead of reloading CR3,
          * we would had to invalidate all the affected virtual addresses
          * this way:
          * invalidate_page(((uptr)i << 22) | ((uptr)j << 12));
          * That is too much. At that point, reloading CR3 is more convenient.
          */
      }


      // alloc memory for the page table

      page_table_t *pt = kmalloc(sizeof(*pt));
      uptr pt_paddr = get_mapping(curr_page_dir, pt);

      // copy the page table
      memmove(pt, orig_pt, sizeof(*pt));

      new_pdir->page_tables[i] = pt;

      // copy the entry, but use the new page table
      new_pdir->entries[i] = pdir->entries[i];
      new_pdir->entries[i].pageTableAddr = pt_paddr >> PAGE_SHIFT;
   }

   for (int i = 768; i < 1024; i++) {
      new_pdir->entries[i] = kernel_page_dir->entries[i];
      new_pdir->page_tables[i] = kernel_page_dir->page_tables[i];
   }

   return new_pdir;
}


void pdir_destroy(page_directory_t *pdir)
{
   // Kernel's pdir cannot be destroyed!
   ASSERT(pdir != kernel_page_dir);

   for (int i = 0; i < 768; i++) {

      page_table_t *pt = pdir->page_tables[i];

      if (pt == NULL) {
         continue;
      }

      for (int j = 0; j < 1024; j++) {

         if (!pt->pages[j].present) {
            continue;
         }

         u32 paddr = pt->pages[j].pageAddr;

         if (pt->pages[j].avail & PAGE_COW_FLAG) {

            ASSERT(pageframes_refcount[paddr] > 0);

            if (pageframes_refcount[paddr] > 1) {
               pageframes_refcount[paddr]--;
               continue;
            }
         }

         // No COW (or COW with ref-count == 1).
         free_pageframe(paddr << PAGE_SHIFT);
      }

      // We freed all the pages, now free the whole page-table.
      kfree(pt, sizeof(*pt));
   }

   // We freed all pages and all the page-tables, now free pdir.
   kfree(pdir, sizeof(*pdir));
}

void init_paging()
{
   set_fault_handler(FAULT_PAGE_FAULT, handle_page_fault);
   set_fault_handler(FAULT_GENERAL_PROTECTION, handle_general_protection_fault);

   kernel_page_dir =
      (page_directory_t *) KERNEL_PA_TO_VA(paging_alloc_pageframe());

   paging_alloc_pageframe(); // The page directory uses 3 pages!
   paging_alloc_pageframe();

   /*
    * The above trick of using 3 consecutive calls of paging_alloc_pageframe()
    * works because in this early stage the frame allocator have all of its
    * frames free and so it is expected to return consecutive frames.
    * In general, expecting to have consecutive physical pages is NOT supported
    * and such tricks should *NOT* be used.
    */

   initialize_page_directory(kernel_page_dir,
                             (uptr) KERNEL_VA_TO_PA(kernel_page_dir),
                             true);

   // Create page entries for the whole 4th GB of virtual memory
   for (int i = 768; i < 1024; i++) {

      u32 page_physical_addr = paging_alloc_pageframe();
      page_table_t *ptable = (void*)KERNEL_PA_TO_VA(page_physical_addr);

      initialize_empty_page_table(ptable);

      page_dir_entry_t e = { 0 };
      e.present = 1;
      e.rw = 1;
      e.us = true;
      e.pageTableAddr = page_physical_addr >> PAGE_SHIFT;

      kernel_page_dir->page_tables[i] = ptable;
      kernel_page_dir->entries[i] = e;
   }

   map_pages(kernel_page_dir,
             (void *)KERNEL_PA_TO_VA(0x1000),
             0x1000, 1024 - 1, false, true);

   ASSERT(debug_count_used_pdir_entries(kernel_page_dir) == 256);
   set_page_directory(kernel_page_dir);

   // Page-size buffer used for COW.
   page_size_buf = (void *) KERNEL_PA_TO_VA(paging_alloc_pageframe());


   /*
    * Allocate the buffer used for keeping a ref-count for each pageframe.
    * This is necessary for COW.
    */

   size_t pagesframes_refcount_bufsize =
      sizeof(pageframes_refcount[0]) * MB
      * get_amount_of_physical_memory_in_mb() / PAGE_SIZE;

   pageframes_refcount = kmalloc(pagesframes_refcount_bufsize);
   memset(pageframes_refcount, 0, pagesframes_refcount_bufsize);
}
