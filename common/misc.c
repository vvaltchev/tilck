/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/assert.h>
#include <tilck/common/string_util.h>
#include <tilck/common/build_info.h>

void
extract_commit_hash_and_date(const struct build_info *bi,
                             struct commit_hash_and_date *c)
{
   const char *comm_end;
   const char *comm;
   size_t len;

   c->dirty = !strncmp(bi->commit, "dirty:", 6);
   comm = bi->commit + (c->dirty ? 6 : 0);
   comm_end = strstr(comm, " ");
   len = comm_end ? (size_t)(comm_end - comm) : strlen(comm);
   len = MIN(len, sizeof(c->hash) - 1);
   memcpy(c->hash, comm, len);
   c->hash[len] = 0;
   c->date[0] = 0;

   if (comm_end) {
      strncpy(c->date, comm_end + 1, sizeof(c->date));
      c->date[sizeof(c->date) - 1] = 0;
   }
}
