/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

struct user_mapping {

   list_node node;

   fs_handle h;
   size_t len;
   size_t off;

   union {
      void *vaddrp;
      uptr vaddr;
   };

   int prot;

};

typedef struct user_mapping user_mapping;
