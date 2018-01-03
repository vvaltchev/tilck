
#include <fs/exvfs.h>
#include <string_util.h>
#include <kmalloc.h>

/* exOS is small: supporting 16 mount points seems more than enough. */
mountpoint *mps[16] = {0};

int mountpoint_add(filesystem *fs, const char *path)
{
   u32 i;

   for (i = 0; i < ARRAY_SIZE(mps); i++) {

      if (!mps[i])
         break;

      if (mps[i]->fs == fs) {
         return -1;
      }

      if (!strcmp(mps[i]->path, path)) {
         return -2;
      }
   }

   VERIFY(i < ARRAY_SIZE(mps));

   const size_t path_len = strlen(path);

   /*
    * Mount points MUST end with '/'.
    */
   ASSERT(path[path_len-1] == '/');

   mountpoint *mp = kmalloc(sizeof(mountpoint) + path_len + 1);
   mp->fs = fs;
   memcpy(mp->path, path, path_len + 1);
   mps[i] = mp;

   return 0;
}

void mountpoint_remove(filesystem *fs)
{
   for (u32 i = 0; i < ARRAY_SIZE(mps); i++) {
      if (mps[i] && mps[i]->fs == fs) {
         kfree(mps[i], sizeof(mountpoint) + strlen(mps[i]->path) + 1);
         mps[i] = NULL;
         return;
      }
   }

   NOT_REACHED();
}

/*
 * Returns:
 *  - 0 in case of non match.
 *  - strlen(mp) in case of a match
 */
STATIC int check_mountpoint_match(const char *mp, const char *path)
{
   int m = 0;
   const size_t lm = strlen(mp);
   const size_t lp = strlen(path);
   const size_t min_len = MIN(lm, lp);

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

   if (mp[m] != 0) {

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

fhandle exvfs_open(const char *path)
{
   fhandle res = {0};
   int best_match_index = -1;
   int best_match_len = 0;

   for (u32 i = 0; i < ARRAY_SIZE(mps); i++) {

      if (!mps[i]) {
         /* Not a valid mount point, skip. */
         continue;
      }

      int len = check_mountpoint_match(mps[i]->path, path);

      if (len > best_match_len) {
         best_match_index = i;
         best_match_len = len;
      }
   }

   if (best_match_index >= 0) {
      res.fs = mps[best_match_index]->fs;
      res.fsh = res.fs->fopen(res.fs, path + best_match_len - 1);
   }

   return res;
}

void exvfs_close(fhandle h)
{
   h.fs->fclose(h.fs, h.fsh);
}

ssize_t exvfs_read(fhandle h, char *buf, size_t buf_size)
{
   return h.fs->fread(h.fs, h.fsh, buf, buf_size);
}

int exvfs_seek(fhandle h, ssize_t off, int whence)
{
   return h.fs->fseek(h.fs, h.fsh, off, whence);
}

size_t exvfs_tell(fhandle h)
{
   return h.fs->ftell(h.fs, h.fsh);
}
