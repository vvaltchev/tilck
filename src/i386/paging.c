
#include <paging.h>
#include <irq.h>
#include <stringUtil.h>
#include <kmalloc.h>
#include <debug_utils.h>

#include <arch/i386/paging_int.h>

/*
 * ----------------------------------------------
 * DEBUG options
 * ----------------------------------------------
 */

bool paging_debug = false;

/* ---------------------------------------------- */

page_directory_t *kernel_page_dir = NULL;

page_directory_t *get_curr_page_dir()
{
   return kernel_page_dir;
}

volatile bool in_page_fault = false;

void handle_page_fault(regs *r)
{
   uint32_t cr2;
   asmVolatile("movl %%cr2, %0" : "=r"(cr2));

   bool us = (r->err_code & (1 << 2)) != 0;
   bool rw = (r->err_code & (1 << 1)) != 0;
   bool p = (r->err_code & (1 << 0)) != 0;

   printk("*** PAGE FAULT in attempt to %s %p from %s %s\n",
          rw ? "WRITE" : "READ",
          cr2,
          us ? "userland" : "kernel",
          !p ? "(NON present page)" : "");

   if (in_page_fault) {
      while (true) {
         halt();
      }
   }

   in_page_fault = true;
   ASSERT(0);
}

void handle_general_protection_fault(regs *r)
{
   printk("General protection fault. Error: %p\n", r->err_code);
}

void set_page_directory(page_directory_t *dir)
{
   asmVolatile("mov %0, %%cr3" :: "r"(KERNEL_VADDR_TO_PADDR(dir)));
}

static void initialize_empty_page_table(page_table_t *t)
{
   for (int i = 0; i < 1024; i++) {
      t->pages[i].present = 0;
      t->pages[i].pageAddr = 0;
   }
}

static void initialize_page_directory(page_directory_t *pdir,
                                      bool us)
{
   page_dir_entry_t not_present = {0};

   not_present.present = 0;
   not_present.rw = 1;
   not_present.us = us;
   not_present.pageTableAddr = 0;

   for (int i = 0; i < 1024; i++) {
      pdir->entries[i] = not_present;
      pdir->page_tables[i] = NULL;
   }
}

bool is_mapped(page_directory_t *pdir, uintptr_t vaddr)
{
   page_table_t *ptable;
   uint32_t page_table_index = (vaddr >> 12) & 0x3FF;
   uint32_t page_dir_index = (vaddr >> 22) & 0x3FF;

   if (pdir->page_tables[page_dir_index] == NULL) {
      return false;
   }

   ptable = pdir->page_tables[page_dir_index];
   return ptable->pages[page_table_index].present;
}

bool unmap_page(page_directory_t *pdir, uintptr_t vaddr)
{
   page_table_t *ptable;
   uint32_t page_table_index = (vaddr >> 12) & 0x3FF;
   uint32_t page_dir_index = (vaddr >> 22) & 0x3FF;

   if (pdir->page_tables[page_dir_index] == NULL) {
      return false;
   }

   ptable = pdir->page_tables[page_dir_index];

   if (!ptable->pages[page_table_index].present) {
      return false;
   }

   page_t p = {0};
   ptable->pages[page_table_index] = p;
   return true;
}

void map_page(page_directory_t *pdir,
	           uintptr_t vaddr,
	           uintptr_t paddr,
              bool us,
              bool rw)
{
   uint32_t page_table_index = (vaddr >> 12) & 0x3FF;
   uint32_t page_dir_index = (vaddr >> 22) & 0x3FF;

   ASSERT(!(vaddr & 4095)); // the vaddr must be page-aligned
   ASSERT(!(paddr & 4095)); // the paddr must be page-aligned

   page_table_t *ptable = NULL;

   if (paging_debug) {
      printk("Mapping vaddr = %p to paddr = %p\n", vaddr, paddr);
   }

   ASSERT(((uintptr_t)pdir->page_tables[page_dir_index] & 0xFFF) == 0);

   if (pdir->page_tables[page_dir_index] == NULL) {

      // we have to create a page table for mapping 'vaddr'

      ptable = KERNEL_PADDR_TO_VADDR(alloc_phys_page());

      if (paging_debug) {
         printk("Creating a new page table at paddr = %p\n"
                "for page_dir_index = %p (= vaddr %p)\n",
                ptable, page_dir_index, page_dir_index << 22);
      }

      initialize_empty_page_table(ptable);

      page_dir_entry_t e = {0};
      e.present = 1;
      e.rw = 1;
      e.us = us;
      e.pageTableAddr = ((uint32_t)KERNEL_VADDR_TO_PADDR(ptable)) >> 12;

      pdir->page_tables[page_dir_index] = ptable;
      pdir->entries[page_dir_index] = e;
   }

   ptable = pdir->page_tables[page_dir_index];

   ASSERT(ptable->pages[page_table_index].present == 0);

   page_t p = {0};
   p.present = 1;
   p.us = us;
   p.rw = rw;

   p.pageAddr = paddr >> 12;

   ptable->pages[page_table_index] = p;
}


void map_pages(page_directory_t *pdir,
	            uintptr_t vaddr,
	            uintptr_t paddr,
               int pageCount,
               bool us,
               bool rw)
{
   for (int i = 0; i < pageCount; i++) {
      map_page(pdir, vaddr + (i << 12), paddr + (i << 12), us, rw);
   }
}

bool kbasic_virtual_alloc(page_directory_t *pdir, uintptr_t vaddr,
                          size_t size, bool us, bool rw)
{
   ASSERT(size > 0);        // the size must be > 0.
   ASSERT(!(size & 4095));  // the size must be a multiple of 4096
   ASSERT(!(vaddr & 4095)); // the vaddr must be page-aligned

   unsigned pagesCount = size >> 12;

   for (unsigned i = 0; i < pagesCount; i++) {
      if (is_mapped(pdir, vaddr + (i << 12))) {
         return false;
      }
   }

   for (unsigned i = 0; i < pagesCount; i++) {

      void *paddr = alloc_phys_page();
      ASSERT(paddr != NULL);

      map_page(pdir, vaddr + (i << 12), (uint32_t)paddr, us, rw);
   }

   return true;
}

bool kbasic_virtual_free(page_directory_t *pdir, uintptr_t vaddr, uintptr_t size)
{
   ASSERT(size > 0);        // the size must be > 0.
   ASSERT(!(size & 4095));  // the size must be a multiple of 4096
   ASSERT(!(vaddr & 4095)); // the vaddr must be page-aligned

   unsigned pagesCount = size >> 12;

   for (unsigned i = 0; i < pagesCount; i++) {
      if (!is_mapped(pdir, vaddr + (i << 12))) {
         return false;
      }
   }

   for (unsigned i = 0; i < pagesCount; i++) {
      unmap_page(pdir, vaddr + (i << 12));
   }

   return true;
}


void init_paging()
{
   set_fault_handler(FAULT_PAGE_FAULT, handle_page_fault);
   set_fault_handler(FAULT_GENERAL_PROTECTION, handle_general_protection_fault);

   kernel_page_dir =
      (page_directory_t *) KERNEL_PADDR_TO_VADDR(alloc_phys_page());

   alloc_phys_page(); // The page directory uses two pages!

   initialize_page_directory(kernel_page_dir, true);

   map_pages(kernel_page_dir,
             KERNEL_PADDR_TO_VADDR(0x1000), 0x1000, 1023, false, true);

   ASSERT(debug_count_used_pdir_entries(kernel_page_dir) == 1);
   set_page_directory(kernel_page_dir);
}
