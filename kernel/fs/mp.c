
#define _TILCK_MP_C_

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/errno.h>

#include "fs_int.h"

typedef struct {

   int curr_mp;

} _mp_cursor;

STATIC_ASSERT(sizeof(_mp_cursor) <= sizeof(uptr) * MP_CURSOR_SIZE_PTRS);

/* Tilck is small-scale: supporting 16 mount points seems more than enough. */
static mountpoint *mps[16];

int mountpoint_add(filesystem *fs, const char *path)
{
   u32 i;
   int rc = 0;

   disable_preemption();

   for (i = 0; i < ARRAY_SIZE(mps); i++) {

      if (!mps[i])
         break; /* we've found a free slot */

      if (mps[i]->fs == fs) {
         rc = -EBUSY;
         goto out;
      }

      if (!strcmp(mps[i]->path, path)) {
         rc = -EBUSY;
         goto out;
      }
   }

   if (i == ARRAY_SIZE(mps)) {
      rc = -ENOMEM;
      goto out;
   }

   const u32 path_len = strlen(path);

   /*
    * Mount points MUST end with '/'.
    */
   ASSERT(path[path_len-1] == '/');

   mountpoint *mp = kmalloc(sizeof(mountpoint) + path_len + 1);

   if (!mp) {
      rc = -ENOMEM;
      goto out;
   }

   mp->fs = fs;
   mp->path_len = path_len;
   memcpy(mp->path, path, path_len + 1);
   mps[i] = mp;

out:
   enable_preemption();
   return rc;
}

void mountpoint_remove(filesystem *fs)
{
   disable_preemption();

   for (u32 i = 0; i < ARRAY_SIZE(mps); i++) {
      if (mps[i] && mps[i]->fs == fs) {
         kfree(mps[i]);
         mps[i] = NULL;
         goto out;
      }
   }

   panic("Unable to find mount point for filesystem at %p", fs);

out:
   enable_preemption();
}

void mountpoint_iter_begin(_mp_cursor *c)
{
   disable_preemption();
   c->curr_mp = 0;
}

void mountpoint_iter_end(_mp_cursor *c)
{
   c->curr_mp = -1;
   enable_preemption();
}

mountpoint *mountpoint_get_next(_mp_cursor *c)
{
   ASSERT(c->curr_mp >= 0);
   ASSERT(!is_preemption_enabled());

   for (u32 i = c->curr_mp++; i < ARRAY_SIZE(mps); i++) {
      if (mps[i] != NULL)
         return mps[i];
   }

   return NULL;
}
