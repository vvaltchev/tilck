
#include <paging.h>
#include <irq.h>
#include <stringUtil.h>
#include <kmalloc.h>

volatile page_directory_t *kernel_page_dir = NULL;

page_directory_t *get_curr_page_dir()
{
   return kernel_page_dir;
}

void handle_page_fault(struct regs *r)
{
   printk("Page fault. Error: %p\n", r->err_code);
}

void handle_general_protection_fault(struct regs *r)
{
   printk("General protection fault. Error: %p\n", r->err_code);
}

void set_page_directory(page_directory_t *dir)
{
   asmVolatile("mov %0, %%cr3" :: "r"(dir->physical_address));
}

static void initialize_empty_page_table(page_table_t *t)
{
   for (int i = 0; i < 1024; i++) {
      t->pages[i].present = 0;
      t->pages[i].pageAddr = 0;
   }
}

static void initialize_page_directory(page_directory_t *pdir,
                                      void *physical_addr,
                                      bool us)
{
   pdir->physical_address = physical_addr;

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

volatile bool paging_debug = false;

void map_page(page_directory_t *pdir,
              uint32_t vaddr,
              uint32_t paddr,
              bool us,
              bool rw)
{
   uint32_t page_table_index = (vaddr >> 12) & 0x3FF;
   uint32_t page_dir_index = (vaddr >> 22) & 0x3FF;

   page_table_t *ptable = NULL;

   if (paging_debug) {
      printk("Mapping vaddr = %p to paddr = %p\n", vaddr, paddr);
      printk("page dir index = %p\n", page_dir_index);
      printk("page table index = %p\n", page_table_index);
      printk("pdir->page_tables[page_dir_index] == %p\n", pdir->page_tables[page_dir_index]);
   }

   ASSERT(((uintptr_t)pdir->page_tables[page_dir_index] & 0xFFF) == 0);

   if (pdir->page_tables[page_dir_index] == NULL) {

      // we have to create a page table for mapping 'vaddr'

      ptable = alloc_phys_page();

      if (paging_debug) {
         printk("Creating a new page table at paddr = %p..\n", ptable);
      }

      initialize_empty_page_table(KERNEL_PADDR_TO_VADDR(ptable));

      page_dir_entry_t e = {0};
      e.present = 1;
      e.rw = 1;
      e.us = us;
      e.pageTableAddr = ((uint32_t)ptable) >> 12;

      pdir->page_tables[page_dir_index] = KERNEL_PADDR_TO_VADDR(ptable);
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

int debug_count_used_pdir_entries(page_directory_t *pdir)
{
   int used = 0;
   for (int i = 0; i < 1024; i++) {
      used += (pdir->page_tables[i] != NULL);
   }
   return used;
}

void debug_dump_used_pdir_entries(page_directory_t *pdir)
{
   printk("Used pdir entries:\n");

   for (int i = 0; i < 1024; i++) {
      if (pdir->page_tables[i] != NULL) {
         printk("Index: %i (= paddr %p)\n", i, (uintptr_t)i << 22);
      }
   }
}

void init_paging()
{
   set_fault_handler(FAULT_PAGE_FAULT, handle_page_fault);
   set_fault_handler(FAULT_GENERAL_PROTECTION, handle_general_protection_fault);

   kernel_page_dir =
      (page_directory_t *) KERNEL_PADDR_TO_VADDR(alloc_phys_page());

   alloc_phys_page();
   alloc_phys_page();

   initialize_page_directory(kernel_page_dir,
                             KERNEL_VADDR_TO_PADDR(kernel_page_dir),
                             true);


   for (uint32_t i = 0; i < 1024; i++) {

      map_page(kernel_page_dir,
               KERNEL_PADDR_TO_VADDR(0x1000 * i),
               0x1000 * i, true, true);

   }

   ASSERT(debug_count_used_pdir_entries(kernel_page_dir) == 1);
   set_page_directory(kernel_page_dir);
}
