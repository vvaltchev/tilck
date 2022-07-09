/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/self_tests.h>
#include <tilck/kernel/paging.h>

/*
 * Special selftest that cannot be run by the system test runner.
 * It has to be run manually.
 */
void selftest_panic(void)
{
   printk("[panic selftest] I'll panic now\n");
   panic("test panic [str: '%s'][num: %d]", "test string", -123);
}

REGISTER_SELF_TEST(panic, se_manual, &selftest_panic)

void selftest_panic2(void)
{
   printk("[panic selftest] I'll panic with bad pointers\n");
   panic("test panic [str: '%s'][num: %d]", (char *)1234, -123);
}

REGISTER_SELF_TEST(panic2, se_manual, &selftest_panic2)

/* This works as expected when the KERNEL_STACK_ISOLATION is enabled */
void selftest_so1(void)
{
   char buf[16];

   /*
    * Hack needed to avoid the compiler detecting that we're accessing the
    * array out-of-bounds, which is generally a terrible bug. But here we're
    * looking exactly for this.
    */
   char *volatile ptr = buf;
   printk("Causing intentionally a stack overflow: expect panic\n");
   memset(ptr, 'x', KERNEL_STACK_SIZE);
}

REGISTER_SELF_TEST(so1, se_manual, &selftest_so1)

/* This works as expected when the KERNEL_STACK_ISOLATION is enabled */
void selftest_so2(void)
{
   char buf[16];

   /*
    * Hack needed to avoid the compiler detecting that we're accessing the
    * array below bounds, which is generally a terrible bug. But here we're
    * looking exactly for this.
    */
   char *volatile ptr = buf;
   printk("Causing intentionally a stack underflow: expect panic\n");
   memset(ptr - KERNEL_STACK_SIZE, 'y', KERNEL_STACK_SIZE);
}

REGISTER_SELF_TEST(so2, se_manual, &selftest_so2)

static void NO_INLINE do_cause_double_fault(void)
{
   char buf[KERNEL_STACK_SIZE];
   memset(buf, 'z', KERNEL_STACK_SIZE);
}

void selftest_so3(void)
{
   printk("Causing intentionally a double fault: expect panic\n");
   do_cause_double_fault();
}

REGISTER_SELF_TEST(so3, se_manual, &selftest_so3)
