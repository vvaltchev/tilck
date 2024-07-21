/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>

/* Standard SBI Errors */
#define SBI_SUCCESS                 0
#define SBI_ERR_FAILURE            -1
#define SBI_ERR_NOT_SUPPORTED      -2
#define SBI_ERR_INVALID_PARAM      -3
#define SBI_ERR_DENIED             -4
#define SBI_ERR_INVALID_ADDRESS    -5
#define SBI_ERR_ALREADY_AVAILABLE  -6
#define SBI_ERR_ALREADY_STARTED    -7
#define SBI_ERR_ALREADY_STOPPED    -8
#define SBI_ERR_NO_SHMEM           -9
#define SBI_ERR_INVALID_STATE      -10
#define SBI_ERR_BAD_RANGE          -11

/* Legacy Extensions */
#define SBI_SET_TIMER                0
#define SBI_CONSOLE_PUTCHAR          1
#define SBI_CONSOLE_GETCHAR          2
#define SBI_CLEAR_IPI                3
#define SBI_SEND_IPI                 4
#define SBI_REMOTE_FENCE_I           5
#define SBI_REMOTE_SFENCE_VMA        6
#define SBI_REMOTE_SFENCE_VMA_ASID   7
#define SBI_SHUTDOWN                 8

/* SBI Base Extension */
#define SBI_BASE_EXT_ID             0x10
#define SBI_BASE_GET_SPEC_VERSION   0
#define SBI_BASE_GET_IMPL_ID        1
#define SBI_BASE_GET_IMPL_VERSION   2
#define SBI_BASE_PROBE_EXTENSION    3
#define SBI_BASE_GET_MVENDORID      4
#define SBI_BASE_GET_MARCHID        5
#define SBI_BASE_GET_MIMPID         6

/* System Reset (SRST) Extension */
#define SBI_SRST_EXT_ID                  0x53525354
#define SBI_SRST_SYSTEM_RESET            0
#define SBI_SRST_TYPE_SHUTDOWN           0
#define SBI_SRST_TYPE_COLD_REBOOT        1
#define SBI_SRST_TYPE_WARM_REBOOT        2
#define SBI_SRST_REASON_NONE             0
#define SBI_SRST_REASON_SYSTEM_FAILURE   1

#define SBI_CALL0(e, f)                     sbi_call(e, f, 0, 0, 0, 0, 0)
#define SBI_CALL1(e, f, a0)                 sbi_call(e, f, a0, 0, 0, 0, 0)
#define SBI_CALL2(e, f, a0, a1)             sbi_call(e, f, a0, a1, 0, 0, 0)
#define SBI_CALL3(e, f, a0, a1, a2)         sbi_call(e, f, a0, a1, a2, 0, 0)
#define SBI_CALL4(e, f, a0, a1, a2, a3)     sbi_call(e, f, a0, a1, a2, a3, 0)
#define SBI_CALL5(e, f, a0, a1, a2, a3, a4) \
   sbi_call(e, f, a0, a1, a2, a3, a4)

struct sbiret {
   long error;
   long value;
};

static ALWAYS_INLINE struct sbiret
sbi_call(ulong eid,
         ulong fid,
         ulong arg0,
         ulong arg1,
         ulong arg2,
         ulong arg3,
         ulong arg4)
{
   struct sbiret rc;

   register ulong a0 __asm__ ("a0") = (ulong)(arg0);
   register ulong a1 __asm__ ("a1") = (ulong)(arg1);
   register ulong a2 __asm__ ("a2") = (ulong)(arg2);
   register ulong a3 __asm__ ("a3") = (ulong)(arg3);
   register ulong a4 __asm__ ("a4") = (ulong)(arg4);
   register ulong a6 __asm__ ("a6") = (ulong)(fid);
   register ulong a7 __asm__ ("a7") = (ulong)(eid);

   asmVolatile("ecall"
               :"+r"(a0), "+r"(a1)
               :"r"(a2), "r"(a3), "r"(a4), "r"(a6), "r"(a7)
               :"memory");

   rc.error = a0;
   rc.value = a1;
   return (rc);
}

static ALWAYS_INLINE long sbi_set_timer(u64 value)
{
   return SBI_CALL1(SBI_SET_TIMER, 0, value).error;
}

static ALWAYS_INLINE long sbi_console_putchar(int ch)
{
   return SBI_CALL1(SBI_CONSOLE_PUTCHAR, 0, ch).error;
}

struct sbiret sbi_get_spec_version(void);
struct sbiret sbi_get_impl_id(void);
struct sbiret sbi_get_impl_version(void);
struct sbiret sbi_get_mvendorid(void);
struct sbiret sbi_get_marchid(void);
struct sbiret sbi_get_mimpid(void);
void sbi_system_reset(u32 type, u32 reason);
void sbi_shutdown(void);
