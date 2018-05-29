
#pragma once
#include <common/basic_defs.h>

typedef struct {

   char vendor_id[16];
   u32 max_basic_cpuid_cmd;

   struct {

      bool fpu;   // 0
      bool vme;   // 1
      bool de;    // 2
      bool pse;   // 3
      bool tsc;   // 4
      bool msr;   // 5
      bool pae;   // 6
      bool mse;   // 7
      bool cx8;   // 8
      bool apic;  // 9

      bool res0;  // 10

      bool sep;   // 11
      bool mtrr;  // 12
      bool pge;   // 13
      bool mca;   // 14
      bool cmov;  // 15
      bool pat;   // 16
      bool pse36; // 17
      bool psn;   // 18
      bool clfsh; // 19

      bool res1;  // 20

      bool ds;    // 21
      bool acpi;  // 22
      bool mmx;   // 23
      bool fxsr;  // 24
      bool sse;   // 25
      bool sse2;  // 26
      bool ss;    // 27
      bool htt;   // 28
      bool tm;    // 29
      bool ia64;  // 30
      bool pbe;   // 31

   } edx1;

   struct {

      bool sse3;         // 0
      bool pclmulqdq;    // 1
      bool dtes64;       // 2
      bool monitor;      // 3
      bool ds_cpl;       // 4
      bool vmx;          // 5
      bool smx;          // 6
      bool est;          // 7
      bool tm2;          // 8
      bool ssse3;        // 9
      bool cnxt_id;      // 10
      bool sdbg;         // 11
      bool fma;          // 12
      bool cx16;         // 13
      bool xtpr;         // 14
      bool pdcm;         // 15

      bool res0;         // 16

      bool pcid;         // 17
      bool dca;          // 18
      bool sse4_1;       // 19
      bool sse4_2;       // 20
      bool x2apic;       // 21
      bool movbe;        // 22
      bool popcnt;       // 23
      bool tsc_deadline; // 24
      bool aes;          // 25
      bool xsave;        // 26
      bool osxsave;      // 27
      bool avx;          // 28
      bool f16c;         // 29
      bool rdrnd;        // 30
      bool hypervisor;   // 31

   } ecx1;

   bool avx2;

   // Features allowed:

   bool can_use_sse;
   bool can_use_sse2;
   bool can_use_avx;
   bool can_use_avx2;

} x86_cpu_features_t;

extern x86_cpu_features_t x86_cpu_features;

void get_x86_cpu_features(void);
void dump_x86_features(void);
