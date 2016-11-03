#include <stdio.h>

void *_kmalloc(size_t);

int main(int argc, char **argv) {

   printf("hello from the kernel unit tests!\n");

   printf("kmalloc(10) returns: %p\n", _kmalloc(10));

   return 0;
}
