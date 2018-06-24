
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/fs/exvfs.h>
#include <exos/kmalloc.h>
#include <exos/errno.h>

/* exOS is small: supporting 16 mount points seems more than enough. */
static mountpoint *mps[16];
static u32 next_device_id;

int mountpoint_add(filesystem *fs, const char *path)
{
   u32 i;

   for (i = 0; i < ARRAY_SIZE(mps); i++) {

      if (!mps[i])
         break; /* we've found a free slot */

      if (mps[i]->fs == fs)
         return -EBUSY;

      if (!strcmp(mps[i]->path, path))
         return -EBUSY;
   }

   if (i == ARRAY_SIZE(mps))
      return -ENOMEM;

   const u32 path_len = strlen(path);

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
         kfree(mps[i]);
         mps[i] = NULL;
         return;
      }
   }

   panic("Unable to find mount point for filesystem at %p", fs);
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

int exvfs_open(const char *path, fs_handle *out)
{
   int rc;
   int best_match_index = -1;
   int best_match_len = 0;

   ASSERT(path != NULL);

   if (*path != '/')
      panic("exvfs_open() works only with absolute paths");

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

   if (best_match_index < 0)
      return -ENOENT;

   filesystem *fs = mps[best_match_index]->fs;

   exvfs_fs_shlock(fs);
   {
      rc = fs->fopen(fs, path + best_match_len - 1, out);
   }
   exvfs_fs_shunlock(fs);

   return rc;
}

void exvfs_close(fs_handle h)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   hb->fs->fclose(h);
}

int exvfs_dup(fs_handle h, fs_handle *dup_h)
{
   fs_handle_base *hb = (fs_handle_base *) h;

   if (!hb)
      return -EBADF;

   int rc = hb->fs->dup(h, dup_h);

   if (rc)
      return rc;

   ASSERT(*dup_h != NULL);
   return 0;
}

ssize_t exvfs_read(fs_handle h, void *buf, size_t buf_size)
{
   fs_handle_base *hb = (fs_handle_base *) h;

   if (!hb->fops.fread)
      return -EINVAL;

   return hb->fops.fread(h, buf, buf_size);
}

ssize_t exvfs_write(fs_handle h, void *buf, size_t buf_size)
{
   fs_handle_base *hb = (fs_handle_base *) h;

   if (!hb->fops.fwrite)
      return -EINVAL;

   return hb->fops.fwrite(h, buf, buf_size);
}

off_t exvfs_seek(fs_handle h, off_t off, int whence)
{
   fs_handle_base *hb = (fs_handle_base *) h;

   if (!hb->fops.fseek)
      return -ESPIPE;

   return hb->fops.fseek(h, off, whence);
}

int exvfs_ioctl(fs_handle h, uptr request, void *argp)
{
   fs_handle_base *hb = (fs_handle_base *) h;

   if (!hb->fops.ioctl)
      return -ENOTTY; // Yes, ENOTTY IS the right error. See the man page.

   return hb->fops.ioctl(h, request, argp);
}

int exvfs_stat(fs_handle h, struct stat *statbuf)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   VERIFY(hb->fops.fstat != NULL); /* stat is NOT optional */
   return hb->fops.fstat(h, statbuf);
}

void exvfs_exlock(fs_handle h)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   ASSERT(hb != NULL);

   if (hb->fops.exlock) {
      hb->fops.exlock(h);
   } else {
      ASSERT(!hb->fops.exunlock);
      exvfs_fs_exlock(get_fs(h));
   }
}

void exvfs_exunlock(fs_handle h)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   ASSERT(hb != NULL);

   if (hb->fops.exunlock) {
      hb->fops.exunlock(h);
   } else {
      ASSERT(!hb->fops.exlock);
      exvfs_fs_exunlock(get_fs(h));
   }
}

void exvfs_shlock(fs_handle h)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   ASSERT(hb != NULL);

   if (hb->fops.shlock) {
      hb->fops.shlock(h);
   } else {
      ASSERT(!hb->fops.shunlock);
      exvfs_fs_shlock(get_fs(h));
   }
}

void exvfs_shunlock(fs_handle h)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   ASSERT(hb != NULL);

   if (hb->fops.shunlock) {
      hb->fops.shunlock(h);
   } else {
      ASSERT(!hb->fops.shlock);
      exvfs_fs_shunlock(get_fs(h));
   }
}

void exvfs_fs_exlock(filesystem *fs)
{
   ASSERT(fs != NULL);
   ASSERT(fs->fs_exlock);

   fs->fs_exlock(fs);
}

void exvfs_fs_exunlock(filesystem *fs)
{
   ASSERT(fs != NULL);
   ASSERT(fs->fs_exunlock);

   fs->fs_exunlock(fs);
}

void exvfs_fs_shlock(filesystem *fs)
{
   ASSERT(fs != NULL);
   ASSERT(fs->fs_shlock);

   fs->fs_shlock(fs);
}

void exvfs_fs_shunlock(filesystem *fs)
{
   ASSERT(fs != NULL);
   ASSERT(fs->fs_shunlock);

   fs->fs_shunlock(fs);
}

static void drop_last_component(char **d_ref, char *const dest)
{
   char *d = *d_ref;

   d--; /* go back to the last written char */
   ASSERT(*d == '/');

   while (d > dest + 1) {

      *d-- = 0;

      if (*d == '/') {

         d++;     /*
                   * advance 'd' before breaking: it points to the next dest
                   * char, not to the last written one.
                   */
         break;
      }
   }

   *d_ref = d;
}

int
compute_abs_path(const char *path, const char *cwd, char *dest, u32 dest_size)
{
   const char *p;
   char *d = dest;

   if (*path != '/') {

      u32 cl = strlen(cwd);
      ASSERT(cl > 0);

      /* The current working directory is ALWAYS supposed to be ending in '/' */
      ASSERT(cwd[cl - 1] == '/');

      if (dest_size < strlen(path) + cl + 1)
         return -1;

      memcpy(dest, cwd, cl + 1);
      d = dest + cl;

   } else {

      /* path is absolute */
      if (dest_size < strlen(path) + 1)
         return -1;
   }


   for (p = path; *p;) {

      if (*p == '/' && p[1] == '/') {
         p++; continue; /* skip multiple slashes */
      }

      if (*p != '.')
         goto copy_char;

      if (p[1] == '/' || !p[1]) {

         p++; // skip '.'

         if (*p)  // the path does NOT end here
            p++;

         continue;

      } else if (p[1] == '.' && (p[2] == '/' || !p[2])) {

         ASSERT(d > dest);

         if (d > dest + 1)
            drop_last_component(&d, dest);
         /*
          * else
          *    dest == "/": we cannot go past that
          */

         p += 2; /* skip ".." */

         if (*p) // the path does NOT end here
            p++;

         continue;

      } else {

copy_char:
         *d++ = *p++; /* default case: just copy the character */
      }
   }

   ASSERT(d > dest);

   if (d > dest + 1 && d[-1] == '/')
      d--; /* drop the trailing '/' */

   ASSERT(d > dest); // dest must contain at least 1 char, '/'.
   *d = 0;
   return 0;
}

u32 exvfs_get_new_device_id(void)
{
   return next_device_id++;
}
