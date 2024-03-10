/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

enum kopt_type {
   KOPT_TYPE_bool,
   KOPT_TYPE_long,
   KOPT_TYPE_ulong,
   KOPT_TYPE_wordstr,
};

/* Used by cmdline.c in the kernel */
struct kopt {
   const char *name;
   const char *alias;
   enum kopt_type type;
   void *data;
};

typedef const char *wordstr;
