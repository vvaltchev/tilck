
#include <paging.h>
#include <irq.h>
#include <stringUtil.h>
#include <kmalloc.h>

volatile page_directory_t *curr_page_dir = NULL;

page_directory_t *get_curr_page_dir()
{
   return (page_directory_t *)curr_page_dir;
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
   curr_page_dir = dir;

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
   not_present.rw = 0;
   not_present.us = us;
   not_present.pageTableAddr = 0;

   for (int i = 0; i < 1024; i++) {
      pdir->entries[i] = not_present;
      pdir->page_tables[i] = NULL;
   }
}

void map_page(page_directory_t *pdir,
              uint32_t vaddr,
              uint32_t paddr,
              bool us,
              bool rw)
{
   //printk("Mapping paddr %p to vaddr %p..\n", paddr, vaddr);

   uint32_t page_table_index = (vaddr >> 12) & 0x3FF;
   uint32_t page_dir_index = (vaddr >> 22) & 0x3FF;

   page_table_t *ptable = NULL;

   //printk("pag dir index: %p\n", page_dir_index);
   //printk("pag table index: %p\n", page_table_index);


   if (pdir->page_tables[page_dir_index] == NULL) {

      //printk("No page table at that index, we have to create a page table\n");

      // we have to create a page table for mapping 'vaddr'

      ptable = alloc_phys_page();
      initialize_empty_page_table(ptable);

      page_dir_entry_t e = {0};
      e.present = 1;
      e.rw = 1;
      e.pageTableAddr = ((uint32_t)ptable) >> 12;

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

void init_paging()
{
   set_fault_handler(FAULT_PAGE_FAULT, handle_page_fault);
   set_fault_handler(FAULT_GENERAL_PROTECTION, handle_general_protection_fault);

   page_directory_t *kernel_page_dir =
      (page_directory_t *)(KERNEL_BASE_VADDR + (char*)alloc_phys_page());

   initialize_page_directory(kernel_page_dir,
                             (char *)kernel_page_dir - KERNEL_BASE_VADDR, true);

   for (uint32_t i = 0; i < 1024; i++) {
      map_page(kernel_page_dir,
               KERNEL_BASE_VADDR + 0x1000 * i,
               0x1000 * i, false, true);
   }

   set_page_directory(kernel_page_dir);
}
