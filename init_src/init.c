
/* Usermode application */

#include <commonDefs.h>
#include "usermode_syscall_wrappers.h"


void init_main()
{
   for (int i = 0; i < 10; ++i) {
      asmVolatile("");
   }

   int ret = generic_usermode_syscall_wrapper3(5, "/myfile.txt", (void*)0xAABB, (void*)0x112233);

   while (ret > 0) {

      generic_usermode_syscall_wrapper3(4, 2, "hello", 0);

      ret--;
   }

   while (1);
}

