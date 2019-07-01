/* SPDX-License-Identifier: BSD-2-Clause */

static filesystem *
get_retained_fs_at(const char *path, const char **fs_path_ref)
{
   mountpoint *mp, *best_match = NULL;
   u32 len, pl, best_match_len = 0;
   filesystem *fs = NULL;
   mp_cursor cur;

   *fs_path_ref = NULL;
   pl = (u32)strlen(path);

   mountpoint_iter_begin(&cur);

   while ((mp = mountpoint_get_next(&cur))) {

      len = mp_check_match(mp->path, mp->path_len, path, pl);

      if (len > best_match_len) {
         best_match = mp;
         best_match_len = len;
      }
   }

   if (best_match) {
      *fs_path_ref = (best_match_len < pl) ? path + best_match_len - 1 : "/";
      fs = best_match->fs;
      retain_obj(fs);
   }

   mountpoint_iter_end(&cur);
   return fs;
}

STATIC int
__vfs_resolve(func_get_entry get_entry,
              const char *path,
              vfs_path *rp,
              bool res_last_sl)
{
   const char *pc;
   void *idir;

   /* the vfs_path `rp` is assumed to be valid */
   ASSERT(rp->fs != NULL);
   ASSERT(rp->fs_path.inode != NULL);

   /* assume the path to be non-empty */
   ASSERT(*path);

   idir = rp->fs_path.inode; /* idir = the initial inode */

   /* if the path starts with '/', the current component starts at path+1 */
   pc = *path == '/' ? ++path : path;

   if (!*path) {
      /* path was just "/" */
      rp->last_comp = path;
      return 0;
   }

   while (*path) {

      if (*path != '/') {
         path++;
         continue;
      }

      /*
       * We hit a slash '/' in the path: we now must lookup this path component.
       * Corner cases:
       *    1. multiple slashes
       *    2. special directory '.'
       *    3. special directory '..'
       */

      while (path[1] == '/') /* handle the case of multiple slashes */
         path++;

      // TODO: handle the other cases.

      get_entry(rp->fs, idir, pc, path - pc, &rp->fs_path);

      if (!rp->fs_path.inode) {

         if (path[1])
            return -ENOENT; /* the path does NOT end here: no such entity */

         /* no such entity, but the path ends here, with a trailing slash */
         break;
      }

      /* We've found an entity for this path component (pc) */

      if (!path[1]) {

         /*
          * The path ends here, with a trailing slash and that's a problem only
          * if `rp->fs_path` is NOT a directory.
          */

         if (rp->fs_path.type != VFS_DIR)
            return -ENOTDIR;

         break;
      }

      idir = rp->fs_path.inode;
      pc = ++path;
   }

   ASSERT(path - pc > 0);

   get_entry(rp->fs, idir, pc, path - pc, &rp->fs_path);
   rp->last_comp = pc;
   return 0;
}

/*
 * Resolves the path, locking the last filesystem with an exclusive or a shared
 * lock depending on `exlock`. The last component of the path, if a symlink, is
 * resolved only with `res_last_sl` is true.
 *
 * NOTE: when the function succeedes (-> return 0), the filesystem is returned
 * as `rp->fs` RETAINED and LOCKED. The caller is supposed to first release the
 * right lock with vfs_shunlock() or with vfs_exunlock() and then to release the
 * FS with release_obj().
 */
STATIC int
vfs_resolve(const char *path, vfs_path *rp, bool exlock, bool res_last_sl)
{
   int rc;
   const char *fs_path;
   func_get_entry get_entry;

   bzero(rp, sizeof(*rp));

   if (!(rp->fs = get_retained_fs_at(path, &fs_path)))
      return -ENOENT;

   get_entry = rp->fs->fsops->get_entry;
   ASSERT(get_entry != NULL);

   /* See the comment in vfs.h about the "fs-lock" funcs */
   exlock ? vfs_fs_exlock(rp->fs) : vfs_fs_shlock(rp->fs);

   /* Get root's entry */
   get_entry(rp->fs, NULL, NULL, 0, &rp->fs_path);
   rc = __vfs_resolve(get_entry, fs_path, rp, res_last_sl);

   if (rc < 0) {
      /* resolve failed: release the lock and the fs */
      exlock ? vfs_fs_exunlock(rp->fs) : vfs_fs_shunlock(rp->fs);
      release_obj(rp->fs); /* it was retained by get_retained_fs_at() */
   }

   return rc;
}
