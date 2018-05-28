
#include <common/basic_defs.h>
#include <common/failsafe_assert.h>

#include <common/string_util.h>
#include <common/arch/generic_x86/x86_utils.h>
#include <common/arch/generic_x86/cpu_features.h>

x86_cpu_features_t x86_cpu_features;

void get_x86_cpu_features(void)
{
   u32 a = 0, b = 0, c = 0, d = 0;
   x86_cpu_features_t *f = &x86_cpu_features;

   cpuid(0, &a, &b, &c, &d);

   f->max_basic_cpuid_cmd = a;

   memcpy(f->vendor_id, &b, 4);
   memcpy(f->vendor_id + 4, &d, 4);
   memcpy(f->vendor_id + 8, &c, 4);

   cpuid(1, &a, &b, &c, &d);

   for (u32 bit = 0; bit < 32; bit++)
      ((bool *)&f->edx1)[bit] = !!(d & (1 << bit));
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

void dump_x86_features(void)
{
   char buf[256];
   u32 w = 0;

   for (u32 i = 0; i < 32; i++) {

      if (!edx1_features[i])
         continue;

      if (((bool *)&x86_cpu_features.edx1)[i])
         w += snprintk(buf + w, sizeof(buf) - w, "%s ", edx1_features[i]);

      if (w >= 60) {
         printk("%s\n", buf);
         w = 0;
         buf[0] = 0;
      }
   }

   if (w)
      printk("%s\n", buf);
}

#endif
