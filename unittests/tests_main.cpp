#include <iostream>
#include <cstdio>
#include <cstdint>
#include <cstdlib>

using namespace std;

extern "C" {

void *kmalloc(size_t);

uintptr_t test_get_heap_base();

void *kernel_heap_base = nullptr;

uintptr_t test_get_heap_base() {

   if (!kernel_heap_base) {
      kernel_heap_base = malloc(128 * 1024 * 1024);
   }

   return (uintptr_t) kernel_heap_base;
}


bool __wrap_kbasic_virtual_alloc(uintptr_t vaddr, size_t size)
{
   printf("[kernel] kbasic_virtual_alloc(%p, %u)\n", vaddr, size);
   return true;
}

bool __wrap_kbasic_virtual_free(uintptr_t vaddr, uintptr_t size)
{
   printf("[kernel] kbasic_virtual_free(%p, %u)\n", vaddr, size);
   return true;
}

}

int main(int argc, char **argv) {

   cout << "hello from C++ 11 kernel unit tests!" << endl;
   printf("kmalloc(10) returns: %p\n", kmalloc(10));

   return 0;
}
