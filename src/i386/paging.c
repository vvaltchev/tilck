
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

#define KERNEL_PADDR_TO_VADDR(paddr) ((typeof(paddr))((uptr)(paddr) + KERNEL_BASE_VADDR))
#define KERNEL_VADDR_TO_PADDR(vaddr) ((typeof(vaddr))((uptr)(vaddr) - KERNEL_BASE_VADDR))

void *paging_alloc_phys_page();
void paging_free_phys_page(void *address);

#define PAGE_COW_FLAG 1
#define PAGE_COW_ORIG_RW 2


/* ---------------------------------------------- */

page_directory_t *kernel_page_dir = NULL;
page_directory_t *curr_page_dir = NULL;


void *page_size_buf = NULL;

volatile bool in_page_fault = false;

void handle_page_fault(regs *r)
{
   u32 vaddr;
   asmVolatile("movl %%cr2, %0" : "=r"(vaddr));

   bool us = (r->err_code & (1 << 2)) != 0;
   bool rw = (r->err_code & (1 << 1)) != 0;
   bool p = (r->err_code & (1 << 0)) != 0;

   if (us && rw && p) {
      page_table_t *ptable;
      u32 page_table_index = (vaddr >> 12) & 0x3FF;
      u32 page_dir_index = (vaddr >> 22) & 0x3FF;

      ptable = curr_page_dir->page_tables[page_dir_index];
      u8 flags = ptable->pages[page_table_index].avail;

      void *page_vaddr = (void *) (vaddr & ~4095);

      if (flags & (PAGE_COW_FLAG | PAGE_COW_ORIG_RW)) {

         printk("*** DEBUG: attempt to write COW page at %p\n", page_vaddr);

         // Copy the whole page to our temporary buffer.
         memmove(page_size_buf, page_vaddr, PAGE_SIZE);

         // Allocate and set a new page.
         uptr paddr = (uptr) alloc_phys_page();
         ptable->pages[page_table_index].pageAddr = paddr >> 12;
         ptable->pages[page_table_index].rw = true;
         ptable->pages[page_table_index].avail = 0;

         invalidate_tlb_page((uptr) ptable);

         // Copy back the page.
         memmove(page_vaddr, page_size_buf, PAGE_SIZE);

         // This is not a "real" page-fault.
         return;
      }
   }


   printk("*** PAGE FAULT in attempt to %s %p from %s %s\n",
          rw ? "WRITE" : "READ",
          vaddr,
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

bool is_mapped(page_directory_t *pdir, uptr vaddr)
{
   page_table_t *ptable;
   u32 page_table_index = (vaddr >> 12) & 0x3FF;
   u32 page_dir_index = (vaddr >> 22) & 0x3FF;

   if (pdir->page_tables[page_dir_index] == NULL) {
      return false;
   }

   ptable = pdir->page_tables[page_dir_index];
   return ptable->pages[page_table_index].present;
}

void unmap_page(page_directory_t *pdir, uptr vaddr)
{
   page_table_t *ptable;
   u32 page_table_index = (vaddr >> 12) & 0x3FF;
   u32 page_dir_index = (vaddr >> 22) & 0x3FF;

   ASSERT(pdir->page_tables[page_dir_index] != NULL);

   ptable = pdir->page_tables[page_dir_index];

   ASSERT(ptable->pages[page_table_index].present);

   page_t p = {0};
   ptable->pages[page_table_index] = p;
}

void *get_mapping(page_directory_t *pdir, uptr vaddr)
{
   page_table_t *ptable;
   u32 page_table_index = (vaddr >> 12) & 0x3FF;
   u32 page_dir_index = (vaddr >> 22) & 0x3FF;

   ASSERT(pdir->page_tables[page_dir_index] != NULL);

   ptable = pdir->page_tables[page_dir_index];

   ASSERT(ptable->pages[page_table_index].present);

   return (void *)(ptable->pages[page_table_index].pageAddr << 12);
}

void map_page(page_directory_t *pdir,
	           uptr vaddr,
	           uptr paddr,
              bool us,
              bool rw)
{
   u32 page_table_index = (vaddr >> 12) & 0x3FF;
   u32 page_dir_index = (vaddr >> 22) & 0x3FF;

   ASSERT(!(vaddr & 4095)); // the vaddr must be page-aligned
   ASSERT(!(paddr & 4095)); // the paddr must be page-aligned

   page_table_t *ptable = NULL;

   ASSERT(((uptr)pdir->page_tables[page_dir_index] & 0xFFF) == 0);

   if (UNLIKELY(pdir->page_tables[page_dir_index] == NULL)) {

      // we have to create a page table for mapping 'vaddr'

      u32 page_physical_addr = (u32)paging_alloc_phys_page();

      ptable = (void*)KERNEL_PADDR_TO_VADDR(page_physical_addr);

      initialize_empty_page_table(ptable);

      page_dir_entry_t e = {0};
      e.present = 1;
      e.rw = 1;
      e.us = us;
      e.pageTableAddr = ((u32)page_physical_addr) >> 12;

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
   new_pdir->paddr = (uptr)get_mapping(curr_page_dir, (uptr)new_pdir);

   page_dir_entry_t not_present = { 0 };

   not_present.present = 0;
   not_present.rw = 1;
   not_present.us = true;
   not_present.pageTableAddr = 0;

   for (int i = 0; i < 768; i++) {

      if (pdir->page_tables[i] == NULL) {
         new_pdir->page_tables[i] = NULL;
         new_pdir->entries[i] = not_present;
         continue;
      }

      // alloc memory for the page table

      page_table_t *pt = kmalloc(sizeof(page_table_t));
      uptr pt_paddr = (uptr)get_mapping(curr_page_dir, (uptr)pt);

      // copy the page table
      memmove(pt, pdir->page_tables[i], sizeof(page_table_t));

      new_pdir->page_tables[i] = pt;

      // copy the entry, but use the new page table
      new_pdir->entries[i] = pdir->entries[i];
      new_pdir->entries[i].pageTableAddr = pt_paddr >> 12;
      
      for (int j = 0; j < 1024; j++) {

         int flags = PAGE_COW_FLAG;

         if (pt->pages[j].rw) {
            flags |= PAGE_COW_ORIG_RW;
         }

         pt->pages[j].avail = flags;
         pt->pages[j].rw = false;
      }
   }

   for (int i = 768; i < 1024; i++) {
      new_pdir->entries[i] = kernel_page_dir->entries[i];
      new_pdir->page_tables[i] = kernel_page_dir->page_tables[i];
   }

   return new_pdir;
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
                             (uptr) KERNEL_VADDR_TO_PADDR(kernel_page_dir), true);

   // Create page entries for the whole 4th GB of virtual memory
   for (int i = 768; i < 1024; i++) {

      u32 page_physical_addr = (u32)paging_alloc_phys_page();

      page_table_t *ptable = (void*)KERNEL_PADDR_TO_VADDR(page_physical_addr);

      initialize_empty_page_table(ptable);

      page_dir_entry_t e = { 0 };
      e.present = 1;
      e.rw = 1;
      e.us = true;
      e.pageTableAddr = ((u32)page_physical_addr) >> 12;

      kernel_page_dir->page_tables[i] = ptable;
      kernel_page_dir->entries[i] = e;
   }

   map_pages(kernel_page_dir,
             KERNEL_PADDR_TO_VADDR(0x1000), 0x1000, 1024 - 1, false, true);

   //printk("debug count used pdir entries: %u\n",
   //       debug_count_used_pdir_entries(kernel_page_dir));

   ASSERT(debug_count_used_pdir_entries(kernel_page_dir) == 256);
   set_page_directory(kernel_page_dir);

   // Page-size buffer used for COW.
   page_size_buf = KERNEL_PADDR_TO_VADDR(paging_alloc_phys_page());
}
