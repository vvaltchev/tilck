
/* Usermode application */

#include <commonDefs.h>
#include "usermode_syscall_wrappers.h"
#include "string.h"

char bigBuf[4096];

void main()
{
   int stackVar;

   for (int i = 0; i < 10; ++i) {
      asmVolatile("" : : : "memory");
   }

   printf("Hello from init!\n");
   printf("&stackVar = %p\n", &stackVar);


   int ret = open("/myfile.txt", 0xAABB, 0x112233);

   printf("ret = %i\n", ret);

   for (int i = 0; i < 5; i++) {
      printf("i = %i\n", i);
   }

   printf("Running infinite loop..\n");

   unsigned n = 1;

   while (true) {

      if (!(n % (1024 * 1024 * 1024))) {

         printf("1 billion iters\n");
         bigBuf[1024] = 'a';
      }

      n++;
   }
}

