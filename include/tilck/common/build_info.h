/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

struct build_info {

   char commit[65];
   char ver[16];
   char arch[16];
};

extern struct build_info tilck_build_info;
