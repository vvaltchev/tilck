/* SPDX-License-Identifier: BSD-2-Clause */

#define _TILCK_MP_C_

#include <tilck/common/string_util.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/syscalls.h>

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
   retain_obj(fs);
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

         release_obj(fs);

         if (get_ref_count(fs) > 0) {
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


/*
 * Returns:
 *  - 0 in case of non match.
 *  - strlen(mp) in case of a match
 */
u32 mp_check_match(const char *mp, u32 lm, const char *path, u32 lp)
{
   u32 m = 0;
   const u32 min_len = MIN(lm, lp);

   /*
    * Mount points MUST end with '/'.
    */
   ASSERT(mp[lm-1] == '/');

   for (size_t i = 0; i < min_len; i++) {

      if (mp[i] != path[i])
         break;

      m++;
   }

   /*
    * We assume that both the paths are absolute. Therefore, at least the
    * initial '/' must match.
    */
   ASSERT(m > 0);

   if (mp[m]) {

      if (mp[m] == '/' && !mp[m + 1] && !path[m]) {
         /* path is like '/dev' while mp is like '/dev/' */
         return m;
      }

      /*
       * The match stopped before the end of mount point's path.
       * Therefore, there is no match.
       */
      return 0;
   }

   if (path[m-1] != '/' && path[m-1] != 0) {

      /*
       * The match stopped before the end of a path component in 'path'.
       * In positive cases, the next character after a match (= position 'm')
       * is either a '/' or \0.
       */

      return 0;
   }

   return m;
}

int
sys_mount(const char *user_source,
          const char *user_target,
          const char *user_filesystemtype,
          unsigned long mountflags,
          const void *user_data)
{
   return -ENOSYS;
}

int sys_umount(const char *target, int flags)
{
   return -ENOSYS;
}
