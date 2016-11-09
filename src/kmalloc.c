#include <kmalloc.h>
#include <paging.h>
#include <stringUtil.h>

void *kmalloc(size_t size)
{
	printk("kmalloc(%d)\n", size);
	return NULL;
}


void kfree(void *ptr, size_t size)
{

}