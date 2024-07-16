/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

struct riscv_cpu_features {

   ulong vendor_id;
   ulong arch_id;
   ulong imp_id;

   struct {

      bool i;
      bool m;
      bool a;
      bool f; // Single-Precision Floating-Point
      bool d; // Double-Precision Floating-Point
      bool q; // Quad-Precision Floating-Point
      bool c;
      bool b;
      bool k;
      bool j;
      bool p;
      bool v;
      bool h;
      bool zicbom;
      bool zicboz;
      bool zicntr;
      bool zicsr;
      bool zifencei;
      bool zihintpause;
      bool zihpm;
      bool zba;
      bool zbb;
      bool zbs;
      bool smaia;
      bool ssaia;
      bool sscofpmf;
      bool sstc;
      bool svinval;
      bool svnapot;
      bool svpbmt;

   } isa_exts;

   /* Vendor defined page attribute bits */
   ulong page_mtmask;
   ulong page_cb; /* Cacheble & bufferable */
   ulong page_wt; /* Write through */
   ulong page_io; /* Strongly-ordered, Non-cacheable, Non-bufferable */
};

extern volatile struct riscv_cpu_features riscv_cpu_features;

void get_cpu_features(void);
void dump_riscv_features(void);
