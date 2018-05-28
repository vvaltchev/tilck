
#include <common/basic_defs.h>

typedef struct {

   char vendor_id[16];
   u32 max_basic_cpuid_cmd;

   struct {

      bool fpu;
      bool vme;
      bool de;
      bool pse;
      bool tsc;
      bool msr;
      bool pae;
      bool mse;
      bool cx8;
      bool apic;

      bool res0;

      bool sep;
      bool mtrr;
      bool pge;
      bool mca;
      bool cmov;
      bool pat;
      bool pse36;
      bool psn;
      bool clfsh;

      bool res1;

      bool ds;
      bool acpi;
      bool mmx;
      bool fxsr;
      bool sse;
      bool sse2;
      bool ss;
      bool htt;
      bool tm;
      bool ia64;
      bool pbe;

   } edx1;

} x86_cpu_features_t;

extern x86_cpu_features_t x86_cpu_features;

void get_x86_cpu_features(void);
void dump_x86_features(void);
