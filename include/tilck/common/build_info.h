/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

struct build_info {

   char commit[160];          /* hash + commit date + tags + [other stuff] */
   char ver[32];              /* string version, like "0.1.0" */
   char arch[32];             /* arch name, like "i686" */
   char extra[32];            /* unused, at the moment */
   char modules_list[256];    /* space-separated list of built-in modules */
};

STATIC_ASSERT(sizeof(struct build_info) == 512);

struct commit_hash_and_date {

   char hash[16];
   char date[26];
   char tags[53];
   bool dirty;
};

extern const struct build_info tilck_build_info;

void
extract_commit_hash_and_date(const struct build_info *bi,
                             struct commit_hash_and_date *c);
