
#include <paging.h>
#include <irq.h>
#include <stringUtil.h>

page_directory_t kernel_page_dir __attribute__((aligned(4096)));
page_table_t kernel_page_table __attribute__((aligned(4096)));

void handle_page_fault(struct regs *r)
{
   printk("Page fault handling..\n");
}

void set_page_directory(page_directory_t *dir)
{
    asmVolatile("mov %0, %%cr3":: "r"(dir->physical_address));

    uint32_t cr0;
    asmVolatile("mov %%cr0, %0": "=r"(cr0));
    cr0 |= 0x80000000; // Enable paging!
    asmVolatile("mov %0, %%cr0":: "r"(cr0));
}

void init_paging()
{
   set_fault_handler(FAULT_PAGE_FAULT, handle_page_fault);

   page_dir_entry_t not_present;
   memset(&not_present, 0, sizeof(not_present));

   not_present.present = 0;
   not_present.rw = 1;
   not_present.us = 0;

   kernel_page_dir.physical_address = &kernel_page_dir;

   for (int i = 0; i < 1024; i++) {
      kernel_page_dir.entries[i] = not_present;
   }

   for (int i = 0; i < 512; i++) {

      page_t page;
      memset(&page, 0, sizeof(page));

      page.present = 1;
      page.rw = 1;
      page.us = 0;

      page.pageAddr = 0x1000 * i;

      kernel_page_table.pages[i] = page;
   }

   page_dir_entry_t kernel_2mb;
   memset(&kernel_2mb, 0, sizeof(kernel_2mb));

   kernel_2mb.present = 1;
   kernel_2mb.rw = 1;
   kernel_2mb.us = 0;
   kernel_2mb.pageTableAddr = ((uint32_t)&kernel_page_table) >> 12;

   kernel_page_dir.entries[0] = kernel_2mb;

   set_page_directory(&kernel_page_dir);
}
