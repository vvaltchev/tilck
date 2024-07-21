/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/utils.h>
#include <tilck/kernel/hal.h>

static bool has_srst_ext = false;

struct sbiret sbi_get_spec_version(void)
{
   return (SBI_CALL0(SBI_BASE_EXT_ID, SBI_BASE_GET_SPEC_VERSION));
}

static long sbi_probe_extension(long eid)
{
   return (SBI_CALL1(SBI_BASE_EXT_ID, SBI_BASE_PROBE_EXTENSION, eid).value);
}

struct sbiret sbi_get_impl_id(void)
{
   return (SBI_CALL0(SBI_BASE_EXT_ID, SBI_BASE_GET_IMPL_ID));
}

struct sbiret sbi_get_impl_version(void)
{
   return (SBI_CALL0(SBI_BASE_EXT_ID, SBI_BASE_GET_IMPL_VERSION));
}

struct sbiret sbi_get_mvendorid(void)
{
   return (SBI_CALL0(SBI_BASE_EXT_ID, SBI_BASE_GET_MVENDORID));
}

struct sbiret sbi_get_marchid(void)
{
   return (SBI_CALL0(SBI_BASE_EXT_ID, SBI_BASE_GET_MARCHID));
}

struct sbiret sbi_get_mimpid(void)
{
   return (SBI_CALL0(SBI_BASE_EXT_ID, SBI_BASE_GET_MIMPID));
}

void sbi_shutdown(void)
{
   SBI_CALL0(SBI_SHUTDOWN, 0);
}

void sbi_system_reset(u32 type, u32 reason)
{
   if (has_srst_ext)
      SBI_CALL2(SBI_SRST_EXT_ID,
                SBI_SRST_SYSTEM_RESET,
                type, reason);

   sbi_shutdown();
}

void sbi_init(void)
{
   if (sbi_get_spec_version().error)
      return; // use legacy extensions

   if (sbi_probe_extension(SBI_SRST_EXT_ID) != 0)
      has_srst_ext = true;
}
