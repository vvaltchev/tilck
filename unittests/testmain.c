#include <stdio.h>

void *kmalloc(size_t);

int main(int argc, char **argv) {

   printf("hello from the kernel unit tests!\n");

   printf("kmalloc(10) returns: %p\n", kmalloc(10));

   return 0;
}
