/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/assert.h>
#include <tilck/common/string_util.h>
#include <tilck/common/build_info.h>

void
extract_commit_hash_and_date(const struct build_info *bi,
                             struct commit_hash_and_date *c)
{
   const char *date, *comm, *tags;
   size_t len;

   c->hash[0] = 0;
   c->date[0] = 0;
   c->tags[0] = 0;

   c->dirty = !strncmp(bi->commit, "dirty:", 6);
   comm = bi->commit + (c->dirty ? 6 : 0);
   len = strlen(comm);
   date = strstr(comm, " ");

   if (date) {
      /* set hash's field length max length */
      len = (size_t)(date - comm);
      date++;
   }

   /* copy the hash field */
   len = MIN(len, sizeof(c->hash) - 1);
   memcpy(c->hash, comm, len);
   c->hash[len] = 0;

   if (!date)
      return;

   /* copy the date field */
   len = 25u; /* fixed length for the date */
   STATIC_ASSERT(sizeof(c->date) > 25u);
   strncpy(c->date, date, len);
   c->date[len] = 0;

   tags = strstr(date, "tags:");

   if (!tags)
      return;

   tags += 5; /* skip "tags:" */

   /* copy the tags field */
   len = MIN(sizeof(c->tags) - 1, strlen(tags));
   memcpy(c->tags, tags, len);
   c->tags[len] = 0;
}
