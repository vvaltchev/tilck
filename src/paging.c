
#include <paging.h>
#include <irq.h>
#include <stringUtil.h>
#include <kmalloc.h>

page_table_t kernel_page_table __attribute__((aligned(4096)));
page_directory_t kernel_page_dir __attribute__((aligned(4096)));


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

   asmVolatile("mov %cr0, %eax");
   asmVolatile("orl 0x80000000, %eax"); // enable paging.
   asmVolatile("mov %eax, %cr0");
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
   printk("Mapping paddr %p to vaddr %p..\n", paddr, vaddr);

   uint32_t page_table_index = (vaddr >> 12) & 0x3FF;
   uint32_t page_dir_index = (vaddr >> 22) & 0x3FF;

   page_table_t *ptable = NULL;

   printk("pag dir index: %p\n", page_dir_index);
   printk("pag table index: %p\n", page_table_index);


   if (pdir->page_tables[page_dir_index] == NULL) {

      printk("No page table at that index, we have to create a page table\n");

      // we have to create a page table for mapping 'vaddr'

      ptable = alloc_phys_page();
      initialize_empty_page_table(ptable);

      page_dir_entry_t e = {0};
      e.present = 1;
      e.rw = 1;
      e.pageTableAddr = ((uint32_t)ptable) >> 12; // physaddr = virtualaddr

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

   initialize_page_directory(&kernel_page_dir, &kernel_page_dir, true);

   for (int i = 0; i < 1024; i++) {

      page_t page = {0};

      page.present = 1;
      page.rw = 1;
      page.us = 1; // temporary allowing user code to see these pages

      page.pageAddr = (0x1000 * i) >> 12;

      kernel_page_table.pages[i] = page;
   }

   page_dir_entry_t kernel_dir_entry;
   memset(&kernel_dir_entry, 0, sizeof(kernel_dir_entry));

   kernel_dir_entry.present = 1;
   kernel_dir_entry.rw = 1;
   kernel_dir_entry.us = 1;  // temporary allowing user code to see these pages
   kernel_dir_entry.pageTableAddr = ((uint32_t)&kernel_page_table) >> 12;

   kernel_page_dir.entries[0] = kernel_dir_entry;


   const char *str = "hello world in 3MB";
   memcpy((void*) 0x300000, str, strlen(str) + 1);

   printk("String at (phys) 0x300000: %s\n", (const char *)0x300000);


   map_page(&kernel_page_dir, 0x900000, 0x300000, true, true);

   set_page_directory(&kernel_page_dir);

   printk("Pagination is ON\n");

   printk("[pagination test] string at 0x900000: %s\n", (const char *)0x900000);

   for (int i = 0; i < 100; i++) {
      printk("%x, ", ((char *)0x900000)[i]);
   }

   printk("\n\n");
}
