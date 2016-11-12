#include <iostream>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cassert>
#include <unordered_map>

using namespace std;

#define PAGE_SIZE (4096)
#define HEAP_SIZE (1 * 1024 * 1024)

extern "C" {

void *kmalloc(size_t);
void kfree(void *ptr, size_t size);

void initialize_kmalloc();

uintptr_t test_get_heap_base();

void *kernel_heap_base = nullptr;

uintptr_t test_get_heap_base() {

   if (!kernel_heap_base) {
      uintptr_t addr = (uintptr_t) malloc(HEAP_SIZE + PAGE_SIZE);
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

bool __wrap_kbasic_virtual_alloc(uintptr_t vaddr, int pageCount)
{
   printf("[test wrap] kbasic_virtual_alloc(%p, %u)\n", vaddr, pageCount);

   assert((vaddr & (PAGE_SIZE - 1)) == 0);

   for (int i = 0; i < pageCount; i++) {
      mappings[vaddr + i * PAGE_SIZE] = true;
   }

   return true;
}

bool __wrap_kbasic_virtual_free(uintptr_t vaddr, int pageCount)
{
   printf("[test wrap] kbasic_virtual_free(%p, %u)\n", vaddr, pageCount);

   assert((vaddr & (PAGE_SIZE - 1)) == 0);

   for (int i = 0; i < pageCount; i++) {
      mappings[vaddr + i * PAGE_SIZE] = false;
   }

   return true;
}

}

void *call_kmalloc_and_print(size_t s)
{
   void *ret = kmalloc(s);
   printf("kmalloc(%u) returns: %p\n", s, (uintptr_t)((char *)ret - (char *)kernel_heap_base));

   return ret;
}

int main(int argc, char **argv) {

   initialize_kmalloc();

   cout << "hello from C++ 11 kernel unit tests!" << endl;
   printf("kernel heap base: %p\n", kernel_heap_base);


   void *b1 = call_kmalloc_and_print(10);
   void *b2 = call_kmalloc_and_print(10);

   void *b3 = call_kmalloc_and_print(50);


   kfree(b1, 10);
   kfree(b2, 10);
   kfree(b3, 50);


   void *b4 = call_kmalloc_and_print(3 * PAGE_SIZE + 43);
   kfree(b4, 3 * PAGE_SIZE + 43);

   return 0;
}
