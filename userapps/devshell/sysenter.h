/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <stdlib.h>

static inline int
do_sysenter_call0(int sysno)
{
   int ret;

#if defined(__i386__)

   asmVolatile ("pushl $1f\n\t"
                "pushl %%ecx\n\t"
                "pushl %%edx\n\t"
                "pushl %%ebp\n\t"
                "movl %%esp, %%ebp\n\t"
                "sysenter\n\t"
                "1:\n\t"
                : "=a" (ret)
                : "a" (sysno)
                : "memory", "cc");

#elif defined(__riscv)

   ret = syscall(sysno);

#else

   abort();

#endif

   return ret;
}

static inline int
do_sysenter_call1(int sysno, void *arg1)
{
   int ret;

#if defined(__i386__)

   asmVolatile ("pushl $1f\n\t"
                "pushl %%ecx\n\t"
                "pushl %%edx\n\t"
                "pushl %%ebp\n\t"
                "movl %%esp, %%ebp\n\t"
                "sysenter\n\t"
                "1:\n\t"
                : "=a" (ret)
                : "a" (sysno), "b" (arg1)
                : "memory", "cc");

#elif defined(__riscv)

   ret = syscall(sysno, arg1);

#else

  abort();

#endif

   return ret;
}

static inline int
do_sysenter_call2(int sysno, void *arg1, void *arg2)
{
   int ret;

#if defined(__i386__)

   asmVolatile ("pushl $1f\n\t"
                "pushl %%ecx\n\t"
                "pushl %%edx\n\t"
                "pushl %%ebp\n\t"
                "movl %%esp, %%ebp\n\t"
                "sysenter\n\t"
                "1:\n\t"
                : "=a" (ret)
                : "a" (sysno), "b" (arg1), "c" (arg2)
                : "memory", "cc");

#elif defined(__riscv)

   ret = syscall(sysno, arg1, arg2);

#else

   abort();

#endif

   return ret;
}

static inline int
do_sysenter_call3(int sysno, void *arg1, void *arg2, void *arg3)
{
   int ret;

#if defined(__i386__)

   asmVolatile ("pushl $1f\n\t"
                "pushl %%ecx\n\t"
                "pushl %%edx\n\t"
                "pushl %%ebp\n\t"
                "movl %%esp, %%ebp\n\t"
                "sysenter\n\t"
                "1:\n\t"
                : "=a" (ret)
                : "a" (sysno), "b" (arg1), "c" (arg2), "d" (arg3)
                : "memory", "cc");

#elif defined(__riscv)

   ret = syscall(sysno, arg1, arg2, arg3);

#else

   abort();

#endif

   return ret;
}

static inline int
do_sysenter_call4(int sysno, void *a1, void *a2, void *a3, void *a4)
{
   int ret;

#if defined(__i386__)

   asmVolatile ("pushl $1f\n\t"
                "pushl %%ecx\n\t"
                "pushl %%edx\n\t"
                "pushl %%ebp\n\t"
                "movl %%esp, %%ebp\n\t"
                "sysenter\n\t"
                "1:\n\t"
                : "=a" (ret)
                : "a" (sysno), "b" (a1), "c" (a2), "d" (a3), "S" (a4)
                : "memory", "cc");

#elif defined(__riscv)

   ret = syscall(sysno, a1, a2, a3, a4);

#else

   abort();

#endif

   return ret;
}

static inline int
do_sysenter_call5(int sysno,
                  void *a1, void *a2, void *a3, void *a4, void *a5)
{
   int ret;

#if defined(__i386__)

   asmVolatile ("pushl $1f\n\t"
                "pushl %%ecx\n\t"
                "pushl %%edx\n\t"
                "pushl %%ebp\n\t"
                "movl %%esp, %%ebp\n\t"
                "sysenter\n\t"
                "1:\n\t"
                : "=a" (ret)
                : "a" (sysno),"b" (a1), "c" (a2), "d" (a3), "S" (a4), "D" (a5)
                : "memory", "cc");

#elif defined(__riscv)

   ret = syscall(sysno, a1, a2, a3, a4, a5);

#else

   abort();

#endif

   return ret;
}


#define sysenter_call0(n) \
   do_sysenter_call0((n))

#define sysenter_call1(n, a1) \
   do_sysenter_call1((n), TO_PTR(a1))

#define sysenter_call2(n, a1, a2) \
   do_sysenter_call2((n), TO_PTR(a1), TO_PTR(a2))

#define sysenter_call3(n, a1, a2, a3) \
   do_sysenter_call3((n), TO_PTR(a1), TO_PTR(a2), TO_PTR(a3))

#define sysenter_call4(n, a1, a2, a3, a4) \
   do_sysenter_call4((n), TO_PTR(a1), TO_PTR(a2), TO_PTR(a3), TO_PTR(a4))

#define sysenter_call5(n, a1, a2, a3, a4, a5) \
   do_sysenter_call5((n),                     \
                     TO_PTR(a1),              \
                     TO_PTR(a2),              \
                     TO_PTR(a3),              \
                     TO_PTR(a4),              \
                     TO_PTR(a5))
