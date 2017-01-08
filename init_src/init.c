
/* Usermode application */

#include "usermode_syscall_wrappers.h"
#include "string.h"

int bss_variable[32];

int main()
{
   int stackVar;

   my_printf("Hello from init!\n");
   my_printf("&stackVar = %p\n", &stackVar);

   for (int i = 0; i < 4; i++) {
      my_printf("BssVar[%i] = %i\n", i, bss_variable[i]);
   }

   int ret = open("/myfile.txt", 0xAABB, 0x112233);

   my_printf("ret = %i\n", ret);

   for (int i = 0; i < 5; i++) {
      my_printf("i = %i\n", i);
   }

   my_printf("Running infinite loop..\n");

   unsigned n = 1;
   int billions = 0;
   bool inchild = false;

   while (true) {

      if (!(n % (1024 * 1024 * 1024))) {

         my_printf("[PID: %i] 1 billion iters\n", getpid());
         billions++;

         if (billions == 1) {

            my_printf("forking..\n");

            int pid = fork();

            my_printf("Fork returned %i\n", pid);

            if (pid == 0) {
               my_printf("############## I'm the child!\n");
               inchild = true;
            } else {
               my_printf("############## I'm the parent, child's pid = %i\n", pid);
            }

         }

         if (billions == 2 && inchild) {
            my_printf("child: 2 billion, exit!\n");
            _exit(123);
         }
      }

      n++;
   }
}

