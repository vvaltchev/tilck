
/* Usermode application */

#include <commonDefs.h>
#include "usermode_syscall_wrappers.h"
#include "string.h"

void init_main()
{
   for (int i = 0; i < 10; ++i) {
      asmVolatile("");
   }

   int ret = open("/myfile.txt", 0xAABB, 0x112233);

   printf("ret = %i\n", ret);

   for (int i = 0; i < 5; i++) {
      printf("i = %i\n", i);
   }

   while (1);
}

