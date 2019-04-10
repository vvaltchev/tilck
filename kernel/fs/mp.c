/* SPDX-License-Identifier: BSD-2-Clause */

#define _TILCK_MP_C_

#include <tilck/common/string_util.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/sync.h>

#include "fs_int.h"

typedef struct {

   int curr_mp;

} _mp_cursor;

STATIC_ASSERT(sizeof(_mp_cursor) <= sizeof(uptr) * MP_CURSOR_SIZE_PTRS);

static mountpoint *mps[MAX_MOUNTPOINTS];
static kmutex mp_mutex = STATIC_KMUTEX_INIT(mp_mutex, 0);

int mountpoint_add(filesystem *fs, const char *path)
{
   u32 i;
   int rc = 0;

   kmutex_lock(&mp_mutex);

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

   const u32 path_len = (u32)strlen(path);

   /*
    * Mount points MUST end with '/'.
    */
   ASSERT(path[path_len - 1] == '/');

   mountpoint *mp = mdalloc(sizeof(mountpoint) + path_len + 1);

   if (!mp) {
      rc = -ENOMEM;
      goto out;
   }

   mp->fs = fs;
   fs->ref_count++;
   mp->path_len = (u32)path_len;
   memcpy(mp->path, path, path_len + 1);
   mps[i] = mp;

out:
   kmutex_unlock(&mp_mutex);
   return rc;
}

void mountpoint_remove(filesystem *fs)
{
   kmutex_lock(&mp_mutex);

   for (u32 i = 0; i < ARRAY_SIZE(mps); i++) {
      if (mps[i] && mps[i]->fs == fs) {

         fs->ref_count--;
         ASSERT(fs->ref_count >= 0);

         if (fs->ref_count > 0) {
            // TODO: [mp] handle unmount of in-use FS by returning an error
            NOT_IMPLEMENTED();
         }

         mdfree(mps[i]);
         mps[i] = NULL;
         goto out;
      }
   }

   panic("Unable to find mount point for filesystem at %p", fs);

out:
   kmutex_unlock(&mp_mutex);
}

void mountpoint_iter_begin(_mp_cursor *c)
{
   kmutex_lock(&mp_mutex);
   c->curr_mp = 0;
}

void mountpoint_iter_end(_mp_cursor *c)
{
   c->curr_mp = -1;
   kmutex_unlock(&mp_mutex);
}

mountpoint *mountpoint_get_next(_mp_cursor *c)
{
   ASSERT(c->curr_mp >= 0);
   ASSERT(kmutex_is_curr_task_holding_lock(&mp_mutex));

   for (int i = c->curr_mp++; i < MAX_MOUNTPOINTS; i++) {
      if (mps[i] != NULL)
         return mps[i];
   }

   return NULL;
}

sptr
sys_mount(const char *user_source,
          const char *user_target,
          const char *user_filesystemtype,
          unsigned long mountflags,
          const void *user_data)
{
   return -ENOSYS;
}

sptr sys_umount(const char *target, int flags)
{
   return -ENOSYS;
}
