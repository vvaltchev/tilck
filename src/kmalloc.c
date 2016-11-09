
#include <kmalloc.h>
#include <paging.h>
#include <stringUtil.h>

#define HEAP_BASE_ADDR (KERNEL_BASE_VADDR + 0x4000000) // BASE + 64 MB

/*
 * Table for accounting the slots of 1 MB used in kernel's virtual memory
 */

bool allocations_table[1024 - 64];


void *kmalloc(size_t size)
{
	printk("kmalloc(%d)\n", size);
	return NULL;
}


void kfree(void *ptr, size_t size)
{
	printk("free(%p, %u)\n", ptr, size);
}