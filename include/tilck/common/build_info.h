/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

struct build_info {

   char commit[65];
   char ver[16];
   char arch[16];
   char modules_list[144];
};

struct commit_hash_and_date {

   char hash[16];
   char date[30];
   bool dirty;
};

extern struct build_info tilck_build_info;

void
extract_commit_hash_and_date(struct build_info *bi,
                             struct commit_hash_and_date *c);
