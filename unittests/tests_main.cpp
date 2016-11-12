#include <iostream>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cassert>
#include <unordered_map>

extern "C" {
#include <kmalloc.h>
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
   //printf("[test wrap] kbasic_virtual_alloc(%p, %u)\n", vaddr, pageCount);

   assert((vaddr & (PAGE_SIZE - 1)) == 0);

   for (int i = 0; i < pageCount; i++) {
      mappings[vaddr + i * PAGE_SIZE] = true;
   }

   return true;
}

bool __wrap_kbasic_virtual_free(uintptr_t vaddr, int pageCount)
{
   //printf("[test wrap] kbasic_virtual_free(%p, %u)\n", vaddr, pageCount);

   assert((vaddr & (PAGE_SIZE - 1)) == 0);

   for (int i = 0; i < pageCount; i++) {
      mappings[vaddr + i * PAGE_SIZE] = false;
   }

   return true;
}

}

//Experiment: how faster is malloc() compared to kmalloc()
//#define kmalloc(x) malloc(x)
//#define kfree(x,s) free(x)

void *call_kmalloc_and_print(size_t s)
{
   void *ret = kmalloc(s);
   //printf("kmalloc(%u) returns: %p\n", s, (uintptr_t)((char *)ret - (char *)HEAP_BASE_ADDR));
   DO_NOT_OPTIMIZE_AWAY(ret);
   return ret;
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

   void *b1,*b2,*b3,*b4;

   uint64_t start = RDTSC();

   const int iters = 1000000;

   for (int i = 0; i < iters; i++) {

      b1 = call_kmalloc_and_print(10);
      b2 = call_kmalloc_and_print(10);
      b3 = call_kmalloc_and_print(50);

      kfree(b1, 10);
      kfree(b2, 10);
      kfree(b3, 50);

      b4 = call_kmalloc_and_print(3 * PAGE_SIZE + 43);
      kfree(b4, 3 * PAGE_SIZE + 43);
   }

   uint64_t duration = (RDTSC() - start) / iters;

   cout << "cycles per malloc + free: " << (duration / 4) << endl;

   ASSERT((uintptr_t)b1 == HEAP_BASE_ADDR + 0x10000);
   ASSERT((uintptr_t)b2 == HEAP_BASE_ADDR + 0x10020);
   ASSERT((uintptr_t)b3 == HEAP_BASE_ADDR + 0x10040);
   ASSERT((uintptr_t)b4 == HEAP_BASE_ADDR + 0x10000);

   return 0;
}
