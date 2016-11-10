
#include <kmalloc.h>
#include <paging.h>
#include <stringUtil.h>

#define SLOTS_COUNT (128) // Total of 128 MB

bool kbasic_virtual_alloc(uintptr_t vaddr, size_t size);
bool kbasic_virtual_free(uintptr_t vaddr, uintptr_t size);

/*
 * Table for accounting slots of 1 MB used in kernel's virtual memory.
 *
 * Each slot is 'false' if there is some space left or 'true' if its whole
 * memory has been used.
 *
 */

bool slots_table[SLOTS_COUNT] = {0};


/*
 * Each bool here is true if the slot has been initialized.
 * Initialized means that the memory for it has been claimed
 */

bool initialized_slots[SLOTS_COUNT] = {0};

int get_free_slot()
{
   for (int i = 0; i < SLOTS_COUNT; i++)
      if (!slots_table[i])
         return i;

   return -1;
}

void *kmalloc(size_t size)
{
	printk("kmalloc(%i)\n", size);
   printk("heap base addr: %p\n", HEAP_BASE_ADDR);

   bool r = kbasic_virtual_alloc(HEAP_BASE_ADDR, 4096);

   ASSERT(r);


	return (void*)HEAP_BASE_ADDR;
}


void kfree(void *ptr, size_t size)
{
	printk("free(%p, %u)\n", ptr, size);
}