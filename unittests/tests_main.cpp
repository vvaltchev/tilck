#include <iostream>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cassert>
#include <unordered_map>

extern "C" {
#include <kmalloc.h>
void kmalloc_trivial_perf_test();
}

using namespace std;

#define PAGE_SIZE (4096)
#define HEAP_SIZE (1 * 1024 * 1024)

extern "C" {


void *kernel_heap_base = nullptr;

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
   assert((vaddr & (PAGE_SIZE - 1)) == 0);

   for (int i = 0; i < pageCount; i++) {
      mappings[vaddr + i * PAGE_SIZE] = true;
   }

   return true;
}

bool __wrap_kbasic_virtual_free(uintptr_t vaddr, int pageCount)
{
   assert((vaddr & (PAGE_SIZE - 1)) == 0);

   for (int i = 0; i < pageCount; i++) {
      mappings[vaddr + i * PAGE_SIZE] = false;
   }

   return true;
}

}

void init_test_kmalloc()
{
   uintptr_t addr = (uintptr_t)malloc(HEAP_SIZE + PAGE_SIZE);
   addr += PAGE_SIZE;
   addr &= ~(PAGE_SIZE - 1);

   kernel_heap_base = (void *)addr;
}

int main(int argc, char **argv) {

   init_test_kmalloc();


   initialize_kmalloc();

   cout << "hello from C++ 11 kernel unit tests!" << endl;
   printf("kernel heap base: %p\n", kernel_heap_base);

   kmalloc_trivial_perf_test();

   return 0;
}
