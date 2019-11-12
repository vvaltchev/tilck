/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/failsafe_assert.h>

#include <tilck/common/string_util.h>
#include <tilck/common/arch/generic_x86/x86_utils.h>
#include <tilck/common/arch/generic_x86/cpu_features.h>

volatile struct x86_cpu_features x86_cpu_features;

void get_cpu_features(void)
{
   STATIC_ASSERT(sizeof(x86_cpu_features.edx1) == 32);
   STATIC_ASSERT(sizeof(x86_cpu_features.ecx1) == 32);

   u32 a = 0, b = 0, c = 0, d = 0;
   struct x86_cpu_features *f = (void *)&x86_cpu_features;

   cpuid(0, &a, &b, &c, &d);

   f->max_basic_cpuid_cmd = a;

   memcpy(f->vendor_id, &b, 4);
   memcpy(f->vendor_id + 4, &d, 4);
   memcpy(f->vendor_id + 8, &c, 4);

   /* If CPUID is supported, CPUID[1] is always supported */
   cpuid(1, &a, &b, &c, &d);

   for (u32 bit = 0; bit < 32; bit++)
      ((bool *)&f->edx1)[bit] = !!(d & (1u << bit));

   for (u32 bit = 0; bit < 32; bit++)
      ((bool *)&f->ecx1)[bit] = !!(c & (1u << bit));

   if (f->max_basic_cpuid_cmd < 7)
      goto ext_features;

   /* CPUID[7] supported */
   if (f->ecx1.avx) {
      cpuid(7, &a, &b, &c, &d);
      f->avx2 = !!(b & (1 << 5)) && !!(b & (1 << 3)) && !!(b & (1 << 8));
   }

ext_features:

   cpuid(0x80000000, &a, &b, &c, &d);
   f->max_ext_cpuid_cmd = a;

   if (f->max_ext_cpuid_cmd < 0x80000007)
      return;

   /* CPUID[0x80000007] supported */
   cpuid(0x80000007, &a, &b, &c, &d);
   f->invariant_TSC = !!(d & (1 << 8));

   if (f->max_ext_cpuid_cmd < 0x80000008)
      return;

   /* CPUID[0x80000008] supported */
   cpuid(0x80000008, &a, &b, &c, &d);
   f->phys_addr_bits = a & 0xff;
   f->virt_addr_bits = (a >> 8) & 0xff;
}

#ifdef __TILCK_KERNEL__

static const char *edx1_features[] =
{
   "fpu",
   "vme",
   "de",
   "pse",
   "tsc",
   "msr",
   "pae",
   "mse",
   "cx8",
   "apic",

   NULL,

   "sep",
   "mtrr",
   "pge",
   "mca",
   "cmov",
   "pat",
   "pse36",
   "psn",
   "clfsh",

   NULL,

   "ds",
   "acpi",
   "mmx",
   "fxsr",
   "sse",
   "sse2",
   "ss",
   "htt",
   "tm",
   "ia64",
   "pbe",
};

static const char *ecx1_features[] =
{
   "sse3",
   "pclmulqdq",
   "dtes64",
   "monitor",
   "ds_cpl",
   "vmx",
   "smx",
   "est",
   "tm2",
   "ssse3",
   "cnxt_id",
   "sdbg",
   "fma",
   "cx16",
   "xtpr",
   "pdcm",

   NULL,

   "pcid",
   "dca",
   "sse41",
   "sse42",
   "x2apic",
   "movbe",
   "popcnt",
   "tsc_deadline",
   "aes",
   "xsave",
   "osxsave",
   "avx",
   "f16c",
   "rdrnd",
   "hypervisor",
};

void dump_x86_features(void)
{
   char buf[256];
   u32 w = 0;

   printk("CPU: %s\n", x86_cpu_features.vendor_id);

   bool *flags[] = {
      (bool *)&x86_cpu_features.edx1,
      (bool *)&x86_cpu_features.ecx1,
   };

   const char **strings[] = {edx1_features, ecx1_features};

   for (u32 j = 0; j < 2; j++) {
      for (u32 i = 0; i < 32; i++) {

         if (!strings[j][i])
            continue;

         if (flags[j][i])
            w += (u32)snprintk(buf + w, sizeof(buf) - w, "%s ", strings[j][i]);

         if (w >= 60) {
            printk("%s\n", buf);
            w = 0;
            buf[0] = 0;
         }
      }
   }

   if (x86_cpu_features.avx2)
      w += (u32)snprintk(buf + w, sizeof(buf) - w, "avx2 ");

   if (w)
      printk("%s\n", buf);
}

#endif
