#include <iostream>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cassert>
#include <unordered_map>

using namespace std;

#define PAGE_SIZE (4096)

extern "C" {

void *kmalloc(size_t);
void initialize_kmalloc();

uintptr_t test_get_heap_base();

void *kernel_heap_base = nullptr;

uintptr_t test_get_heap_base() {

   if (!kernel_heap_base) {
      uintptr_t addr = (uintptr_t) malloc(128 * 1024 * 1024 + PAGE_SIZE);
      addr += PAGE_SIZE;
      addr &= ~(PAGE_SIZE - 1);

      kernel_heap_base = (void *) addr;
   }

   return (uintptr_t) kernel_heap_base;
}

void *__wrap_get_kernel_page_dir()
{
   return nullptr;
}

unordered_map<uintptr_t, bool> mappings;

bool __wrap_is_mapped(void *pdir, uintptr_t vaddr)
{
   return mappings[vaddr & ~(PAGE_SIZE - 1)];
}

bool __wrap_kbasic_virtual_alloc(uintptr_t vaddr, size_t size)
{
   printf("[test wrap] ********************* kbasic_virtual_alloc(%p, %u) *************************\n", vaddr, size);

   assert((size & (PAGE_SIZE - 1)) == 0);
   assert((vaddr & (PAGE_SIZE - 1)) == 0);

   int pageCount = (int) (size >> 12);

   for (int i = 0; i < pageCount; i++) {
      mappings[vaddr + i * PAGE_SIZE] = true;
   }

   return true;
}

bool __wrap_kbasic_virtual_free(uintptr_t vaddr, uintptr_t size)
{
   printf("[test wrap] kbasic_virtual_free(%p, %u)\n", vaddr, size);

   assert((size & (PAGE_SIZE - 1)) == 0);
   assert((vaddr & (PAGE_SIZE - 1)) == 0);

   int pageCount = (int)(size >> 12);

   for (int i = 0; i < pageCount; i++) {
      mappings[vaddr + i * PAGE_SIZE] = false;
   }

   return true;
}

}

int main(int argc, char **argv) {

   initialize_kmalloc();

   cout << "hello from C++ 11 kernel unit tests!" << endl;
   printf("kernel heap base: %p\n", kernel_heap_base);

{
   void *ret = kmalloc(10);
   uintptr_t val = (uintptr_t) ((char *)ret - (char *)kernel_heap_base);
   printf("kmalloc(10) returns: %p\n", (void *) val);
}

{
   void *ret = kmalloc(10);
   uintptr_t val = (uintptr_t)((char *)ret - (char *)kernel_heap_base);
   printf("kmalloc(10) returns: %p\n", (void *)val);
}


{
   void *ret = kmalloc(50);
   uintptr_t val = (uintptr_t)((char *)ret - (char *)kernel_heap_base);
   printf("kmalloc(50) returns: %p\n", (void *)val);
}


   return 0;
}
