
#include <kmalloc.h>
#include <paging.h>
#include <stringUtil.h>

#define HEAP_BASE_ADDR (KERNEL_BASE_VADDR + 0x4000000) // BASE + 64 MB
#define SLOTS_COUNT (128)


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

      map_page(pdir, vaddr + (i << 12), (uintptr_t)paddr, us, rw);
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

/*
 * Table for accounting slots of 1 MB used in kernel's virtual memory.
 *
 * Each slot is 'false' if there is some space left or 'true' if its whole
 * memory has been used.
 *
 */

bool slots_table[SLOTS_COUNT];


/*
 * Each bool here is true if the slot has been initialized.
 * Initialized means that the memory for it has been claimed
 */

bool initialized_slots[SLOTS_COUNT];

int get_free_slot()
{
   for (int i = 0; i < SLOTS_COUNT; i++) {
      if (!slots_table[i])
         return i;
   }

   return -1;
}

void *kmalloc(size_t size)
{
	printk("kmalloc(%d)\n", size);

   

	return NULL;
}


void kfree(void *ptr, size_t size)
{
	printk("free(%p, %u)\n", ptr, size);
}