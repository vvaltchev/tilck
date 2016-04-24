
#include <commonDefs.h>

#define SYSCALL_RESTART   0
#define SYSCALL_EXIT      1
#define SYSCALL_FORK      2
#define SYSCALL_READ      3
#define SYSCALL_WRITE     4
#define SYSCALL_OPEN      5
#define SYSCALL_CLOSE     6
#define SYSCALL_WAITPID   7


static int generic_syscall0(int syscall_num)
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

static int generic_syscall1(int syscall_num, void *arg1)
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

static int generic_syscall2(int syscall_num, void *arg1, void *arg2)
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


static int generic_syscall3(int syscall_num, void *arg1, void *arg2, void *arg3)
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

static int open(const char *pathname, int32_t flags, int32_t mode)
{
   return generic_syscall3(SYSCALL_OPEN,
                           (void*)pathname,
                           (void*)flags,
                           (void*) mode);
}

static int write(int fd, const void *buf, size_t count)
{
   return generic_syscall3(SYSCALL_WRITE,
                           (void*)fd,
                           (void*)buf,
                           (void*)count);
}

static int read(int fd, const void *buf, size_t count)
{
   return generic_syscall3(SYSCALL_READ,
                           (void*)fd,
                           (void*)buf,
                           (void*)count);
}

static int close(int fd)
{
   return generic_syscall1(SYSCALL_CLOSE, (void *)fd);
}

