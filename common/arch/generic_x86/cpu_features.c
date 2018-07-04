
#include <exos/common/basic_defs.h>
#include <exos/common/failsafe_assert.h>

#include <exos/common/string_util.h>
#include <exos/common/arch/generic_x86/x86_utils.h>
#include <exos/common/arch/generic_x86/cpu_features.h>

volatile x86_cpu_features_t x86_cpu_features;

void get_x86_cpu_features(void)
{
   u32 a = 0, b = 0, c = 0, d = 0;
   x86_cpu_features_t *f = (void *)&x86_cpu_features;

   cpuid(0, &a, &b, &c, &d);

   f->max_basic_cpuid_cmd = a;

   memcpy(f->vendor_id, &b, 4);
   memcpy(f->vendor_id + 4, &d, 4);
   memcpy(f->vendor_id + 8, &c, 4);

   cpuid(1, &a, &b, &c, &d);

   for (u32 bit = 0; bit < 32; bit++)
      ((bool *)&f->edx1)[bit] = !!(d & (1 << bit));

   for (u32 bit = 0; bit < 32; bit++)
      ((bool *)&f->ecx1)[bit] = !!(c & (1 << bit));

   if (f->max_basic_cpuid_cmd >= 7) {
      if (f->ecx1.avx) {
         cpuid(7, &a, &b, &c, &d);
         f->avx2 = !!(b & (1 << 5)) && !!(b & (1 << 3)) && !!(b & (1 << 8));
      }
   }
}

#ifdef __EXOS_KERNEL__

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
   "pbe"
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
   "hypervisor"
};

void dump_x86_features(void)
{
   char buf[256];
   u32 w = 0;

   printk("CPU: %s\n", x86_cpu_features.vendor_id);

   bool *flags[] = {
      (bool *)&x86_cpu_features.edx1,
      (bool *)&x86_cpu_features.ecx1
   };

   const char **strings[] = {edx1_features, ecx1_features};

   for (u32 j = 0; j < 2; j++) {
      for (u32 i = 0; i < 32; i++) {

         if (!strings[j][i])
            continue;

         if (flags[j][i])
            w += snprintk(buf + w, sizeof(buf) - w, "%s ", strings[j][i]);

         if (w >= 60) {
            printk("%s\n", buf);
            w = 0;
            buf[0] = 0;
         }
      }
   }

   if (x86_cpu_features.avx2)
      w += snprintk(buf + w, sizeof(buf) - w, "avx2 ");

   if (w)
      printk("%s\n", buf);
}

#endif
