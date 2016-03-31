
#include <commonDefs.h>

static int generic_usermode_syscall_wrapper0(int syscall_num)
{
   int result;
   asmVolatile("movl %0, %%eax\n"
               "int $0x80\n"
               : // no output
               : "m"(syscall_num)
               : "%eax");

   asm("movl %%eax, %0": "=a"(result));
   return result;
}

static int generic_usermode_syscall_wrapper1(int syscall_num, void *arg1)
{
   int result;
   asmVolatile("movl %0, %%eax\n"
               "movl %1, %%ebx\n"
               "int $0x80\n"
               : // no output
               : "m"(syscall_num), "m"(arg1)
               : "%eax", "%ebx");

   asm("movl %%eax, %0": "=a"(result));
   return result;
}

static int generic_usermode_syscall_wrapper2(int syscall_num, void *arg1, void *arg2)
{
   int result;
   asmVolatile("movl %0, %%eax\n"
               "movl %1, %%ebx\n"
               "movl %2, %%ecx\n"
               "int $0x80\n"
               : // no output
               : "m"(syscall_num), "m"(arg1), "m"(arg2)
               : "%eax", "%ebx", "%ecx");

   asm("movl %%eax, %0": "=a"(result));
   return result;
}


static int generic_usermode_syscall_wrapper3(int syscall_num, void *arg1, void *arg2, void *arg3)
{
   int result;
   asmVolatile("movl %0, %%eax\n"
               "movl %1, %%ebx\n"
               "movl %2, %%ecx\n"
               "movl %3, %%edx\n"
               "int $0x80\n"
               : // no output
               : "m"(syscall_num), "m"(arg1), "m"(arg2), "m"(arg3)
               : "%eax", "%ebx", "%ecx", "%edx");

   asm("movl %%eax, %0": "=a"(result));
   return result;
}
