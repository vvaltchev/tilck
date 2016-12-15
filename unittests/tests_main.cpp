#include <cstdio>

using namespace std;

extern "C" {
   void kernel_kmalloc_perf_test();
   void init_pageframe_allocator();
   void initialize_kmalloc();      
}

void init_test_kmalloc();
void kmalloc_chaos_test();

void inititalize_kernel_data_structures()
{
   init_pageframe_allocator();
   initialize_kmalloc();   
}

int main(int argc, char **argv)
{
   // Initialize mock-up, before kernel itself
   init_test_kmalloc();

   // Run the kernel initialization
   inititalize_kernel_data_structures();

   kmalloc_chaos_test();
   kernel_kmalloc_perf_test();

   return 0;
}
