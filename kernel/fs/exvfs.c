
#include <fs/exvfs.h>
#include <string_util.h>
#include <kmalloc.h>

/* exOS is small: supporting 16 mount points seems more than enough. */
mountpoint *mps[16] = {0};

void mountpoint_add(filesystem *fs, const char *path)
{
   u32 i;

   for (i = 0; i < ARRAY_SIZE(mps); i++)
      if (!mps[i])
         break;

   VERIFY(i < ARRAY_SIZE(mps));

   size_t path_len = strlen(path);
   mountpoint *mp = kmalloc(sizeof(mountpoint) + path_len + 1);
   mp->fs = fs;
   memcpy(mp->path, path, path_len + 1);
   mps[i] = mp;
}

void mountpoint_remove(filesystem *fs)
{
   for (u32 i = 0; i < ARRAY_SIZE(mps); i++) {
      if (mps[i] && mps[i]->fs == fs) {
         kfree(mps, sizeof(mountpoint) + strlen(mps[i]->path) + 1);
         mps[i] = NULL;
         break;
      }
   }

   NOT_REACHED();
}
