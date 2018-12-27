/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

static inline int
do_sysenter_call0(int syscall)
{
   int ret;

   __asm__ volatile ("pushl $1f\n\t"
                     "pushl %%ecx\n\t"
                     "pushl %%edx\n\t"
                     "pushl %%ebp\n\t"
                     "movl %%esp, %%ebp\n\t"
                     "sysenter\n\t"
                     "1:\n\t"
                     : "=a" (ret)
                     : "a" (syscall)
                     : "memory", "cc");

   return ret;
}

static inline int
do_sysenter_call1(int syscall, void *arg1)
{
   int ret;

   __asm__ volatile ("pushl $1f\n\t"
                     "pushl %%ecx\n\t"
                     "pushl %%edx\n\t"
                     "pushl %%ebp\n\t"
                     "movl %%esp, %%ebp\n\t"
                     "sysenter\n\t"
                     "1:\n\t"
                     : "=a" (ret)
                     : "a" (syscall), "b" (arg1)
                     : "memory", "cc");

   return ret;
}

static inline int
do_sysenter_call2(int syscall, void *arg1, void *arg2)
{
   int ret;

   __asm__ volatile ("pushl $1f\n\t"
                     "pushl %%ecx\n\t"
                     "pushl %%edx\n\t"
                     "pushl %%ebp\n\t"
                     "movl %%esp, %%ebp\n\t"
                     "sysenter\n\t"
                     "1:\n\t"
                     : "=a" (ret)
                     : "a" (syscall), "b" (arg1), "c" (arg2)
                     : "memory", "cc");

   return ret;
}

static inline int
do_sysenter_call3(int syscall, void *arg1, void *arg2, void *arg3)
{
   int ret;

   __asm__ volatile ("pushl $1f\n\t"
                     "pushl %%ecx\n\t"
                     "pushl %%edx\n\t"
                     "pushl %%ebp\n\t"
                     "movl %%esp, %%ebp\n\t"
                     "sysenter\n\t"
                     "1:\n\t"
                     : "=a" (ret)
                     : "a" (syscall), "b" (arg1), "c" (arg2), "d" (arg3)
                     : "memory", "cc");

   return ret;
}

#define sysenter_call0(n) \
   do_sysenter_call0((n))

#define sysenter_call1(n, a1) \
   do_sysenter_call1((n), (void*)(a1))

#define sysenter_call2(n, a1, a2) \
   do_sysenter_call2((n), (void*)(a1), (void*)(a2))

#define sysenter_call3(n, a1, a2, a3) \
   do_sysenter_call3((n), (void*)(a1), (void*)(a2), (void*)(a3))
