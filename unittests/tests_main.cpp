#include <iostream>
#include <cstdio>

using namespace std;

extern "C" {
   void *kmalloc(size_t);
}

int main(int argc, char **argv) {

   cout << "hello from C++ 11 kernel unit tests!" << endl;
   printf("kmalloc(10) returns: %p\n", kmalloc(10));

   return 0;
}
