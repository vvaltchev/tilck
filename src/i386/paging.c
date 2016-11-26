
#include <arch/i386/arch_utils.h>
#include <paging.h>
#include <irq.h>
#include <stringUtil.h>
#include <kmalloc.h>
#include <debug_utils.h>

#include <arch/i386/paging_int.h>

/*
 * Theese MACROs can be used only for the first 4 MB of the kernel virtual address space.
 */

#define KERNEL_PADDR_TO_VADDR(paddr) ((typeof(paddr))((uintptr_t)(paddr) + KERNEL_BASE_VADDR))
#define KERNEL_VADDR_TO_PADDR(vaddr) ((typeof(vaddr))((uintptr_t)(vaddr) - KERNEL_BASE_VADDR))

void *paging_alloc_phys_page();
void paging_free_phys_page(void *address);


/*
 * ----------------------------------------------
 * DEBUG options
 * ----------------------------------------------
 */

//bool paging_debug = false;

/* ---------------------------------------------- */

page_directory_t *kernel_page_dir = NULL;
page_directory_t *curr_page_dir = NULL;

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

void initialize_page_directory(page_directory_t *pdir, uintptr_t paddr, bool us)
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

void unmap_page(page_directory_t *pdir, uintptr_t vaddr)
{
   page_table_t *ptable;
   uint32_t page_table_index = (vaddr >> 12) & 0x3FF;
   uint32_t page_dir_index = (vaddr >> 22) & 0x3FF;

   ASSERT(pdir->page_tables[page_dir_index] != NULL);

   ptable = pdir->page_tables[page_dir_index];

   ASSERT(ptable->pages[page_table_index].present);

   page_t p = {0};
   ptable->pages[page_table_index] = p;
}

void *get_mapping(page_directory_t *pdir, uintptr_t vaddr)
{
   page_table_t *ptable;
   uint32_t page_table_index = (vaddr >> 12) & 0x3FF;
   uint32_t page_dir_index = (vaddr >> 22) & 0x3FF;

   ASSERT(pdir->page_tables[page_dir_index] != NULL);

   ptable = pdir->page_tables[page_dir_index];

   ASSERT(ptable->pages[page_table_index].present);

   return (void *)(ptable->pages[page_table_index].pageAddr << 12);
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

   //if (paging_debug) {
   //   printk("Mapping vaddr = %p to paddr = %p\n", vaddr, paddr);
   //}

   ASSERT(((uintptr_t)pdir->page_tables[page_dir_index] & 0xFFF) == 0);

   if (UNLIKELY(pdir->page_tables[page_dir_index] == NULL)) {

      // we have to create a page table for mapping 'vaddr'

      uint32_t page_physical_addr = (uint32_t)paging_alloc_phys_page();

      ptable = (void*)KERNEL_PADDR_TO_VADDR(page_physical_addr);

      //if (paging_debug) {
      //   printk("Creating a new page table at paddr = %p\n"
      //          "for page_dir_index = %p (= vaddr %p)\n",
      //          ptable, page_dir_index, page_dir_index << 22);
      //}

      initialize_empty_page_table(ptable);

      page_dir_entry_t e = {0};
      e.present = 1;
      e.rw = 1;
      e.us = us;
      e.pageTableAddr = ((uint32_t)page_physical_addr) >> 12;

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

page_directory_t *pdir_clone(page_directory_t *pdir)
{
   page_directory_t *new_pdir = kmalloc(sizeof(page_directory_t));

   for (int i = 0; i < 1024; i++) {

      if (pdir->page_tables[i] == NULL) {
         continue;
      }
      
      // TODO: finish the implementation.
      ASSERT(0);
   }

   return new_pdir;
}

void add_kernel_base_mappings(page_directory_t *pdir)
{
   for (int i = 767; i < 1024; i++) {
      pdir->entries[i] = kernel_page_dir->entries[i];
      pdir->page_tables[i] = kernel_page_dir->page_tables[i];
   }
}

void init_paging()
{
   set_fault_handler(FAULT_PAGE_FAULT, handle_page_fault);
   set_fault_handler(FAULT_GENERAL_PROTECTION, handle_general_protection_fault);

   kernel_page_dir =
      (page_directory_t *) KERNEL_PADDR_TO_VADDR(paging_alloc_phys_page());

   paging_alloc_phys_page(); // The page directory uses 3 pages!
   paging_alloc_phys_page();

   initialize_page_directory(kernel_page_dir,
                             (uintptr_t) KERNEL_VADDR_TO_PADDR(kernel_page_dir), false);

   // Create page entries for the whole 4th GB of virtual memory
   for (int i = 768; i < 1024; i++) {

      uint32_t page_physical_addr = (uint32_t)paging_alloc_phys_page();

      page_table_t *ptable = (void*)KERNEL_PADDR_TO_VADDR(page_physical_addr);

      initialize_empty_page_table(ptable);

      page_dir_entry_t e = { 0 };
      e.present = 1;
      e.rw = 1;
      e.us = false;
      e.pageTableAddr = ((uint32_t)page_physical_addr) >> 12;

      kernel_page_dir->page_tables[i] = ptable;
      kernel_page_dir->entries[i] = e;
   }

   map_pages(kernel_page_dir,
             KERNEL_PADDR_TO_VADDR(0x1000), 0x1000, 1024 - 1, false, true);

   printk("debug count used pdir entries: %u\n",
          debug_count_used_pdir_entries(kernel_page_dir));

   ASSERT(debug_count_used_pdir_entries(kernel_page_dir) == 256);
   set_page_directory(kernel_page_dir);
}
