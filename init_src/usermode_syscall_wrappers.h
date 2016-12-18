
#pragma once

#include <commonDefs.h>
#include "syscall_interface.h"

#define SYSCALL_RESTART   0
#define SYSCALL_EXIT      1
#define SYSCALL_FORK      2
#define SYSCALL_READ      3
#define SYSCALL_WRITE     4
#define SYSCALL_OPEN      5
#define SYSCALL_CLOSE     6
#define SYSCALL_WAITPID   7


static ALWAYS_INLINE int fork()
{
   return generic_syscall0(SYSCALL_FORK);
}

static ALWAYS_INLINE void _exit(int code)
{
   generic_syscall1(SYSCALL_EXIT, (void *) code);
}

static ALWAYS_INLINE int open(const char *pathname, s32 flags, s32 mode)
{
   return generic_syscall3(SYSCALL_OPEN,
                           (void*)pathname,
                           (void*)flags,
                           (void*)mode);
}

static ALWAYS_INLINE int write(int fd, const void *buf, size_t count)
{
   return generic_syscall3(SYSCALL_WRITE,
                           (void*)fd,
                           (void*)buf,
                           (void*)count);
}

static ALWAYS_INLINE int read(int fd, const void *buf, size_t count)
{
   return generic_syscall3(SYSCALL_READ,
                           (void*)fd,
                           (void*)buf,
                           (void*)count);
}

static ALWAYS_INLINE int close(int fd)
{
   return generic_syscall1(SYSCALL_CLOSE, (void *)fd);
}

