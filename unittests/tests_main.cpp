#include <cstdio>
#include <gtest/gtest.h>

using namespace std;
using namespace testing;

extern "C" {
   void init_pageframe_allocator();
   void initialize_kmalloc();      
}

void init_test_kmalloc();

void inititalize_kernel_data_structures()
{
   init_pageframe_allocator();
   initialize_kmalloc();   
}

int main(int argc, char **argv)
{
   InitGoogleTest(&argc, argv);

   // Initialize mock-ups, before kernel itself
   init_test_kmalloc();

   // Run the kernel initialization
   inititalize_kernel_data_structures();

   return RUN_ALL_TESTS();
}
