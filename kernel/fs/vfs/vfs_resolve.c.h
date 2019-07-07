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

STATIC const char *
__vfs_res_handle_dot_slash(const char *path)
{
   /* Handle the case of multiple slashes. */
   while (path[1] == '/')
      path++;

   /* Handle the case of a single '.' */
   if (path[1] == '.') {

      if (!path[2])
         return NULL; /* NULL means return 0 in the caller */

      if (path[2] == '/') {
         path += 2;   /* just skip the "/." substring */
      }
   }

   return path;
}

STATIC int
__vfs_resolve(func_get_entry get_entry,
              const char *path,
              vfs_path *rp,
              bool res_last_sl)
{
   const fs_path_struct orig_fs_path = rp->fs_path;
   const char *const orig_path = path;
   void *idir[32];
   u8 pc_offs[32];
   uptr ss = 0;        /* stack size */

   /* the vfs_path `rp` is assumed to be valid */
   ASSERT(rp->fs != NULL);
   ASSERT(rp->fs_path.inode != NULL);

   if (!*path)
      return -ENOENT;

   rp->last_comp = path;

   if (!(path = __vfs_res_handle_dot_slash(path - 1)))
      return 0;

   ++path;
   pc_offs[ss] = (u8) (path - orig_path);
   idir[ss] = rp->fs_path.inode; /* idir = the initial inode */
   ss++;

   if (!*path) {
      /* path was just "/" */
      rp->last_comp = path;
      return 0;
   }

   while (*path) {

      if (path[0] == '.' && path[1] == '.' && (!path[2] || path[2] == '/')) {

         if (ss > 2) {

            ss--;
            const char *pc = orig_path + (sptr) pc_offs[ss - 1];
            const char *oldpc = orig_path + (sptr) pc_offs[ss - 2];
            get_entry(rp->fs, idir[ss-2], oldpc, pc-oldpc-1, &rp->fs_path);
            rp->last_comp = oldpc;

         } else {

            rp->fs_path = orig_fs_path;

            if (ss > 1)
               ss--;
         }

         path += 2;

         if (!path[0] || (path[0] == '/' && !path[1]))
            return 0;

         path++;
         continue;
      }

      if (*path != '/') {
         path++;
         continue;
      }


      {
         const char *pc = orig_path + (sptr) pc_offs[ss - 1];
         get_entry(rp->fs, idir[ss-1], pc, path - pc, &rp->fs_path);
         rp->last_comp = pc;
      }

      if (!(path = __vfs_res_handle_dot_slash(path)))
         return 0;

      if (!rp->fs_path.inode) {
         return path[1]
            ? -ENOENT /* the path does NOT end here: no such entity */
            : 0;      /* the path just ends with a trailing slash */
      }

      /* We've found an entity for this path component (pc) */

      if (!path[1]) {

         /* The path ends here, with a trailing slash */
         return rp->fs_path.type != VFS_DIR
            ? -ENOTDIR /* if the entry is not a dir, that's a problem */
            : 0;
      }

      if (ss == ARRAY_SIZE(idir))
         return -ENAMETOOLONG;

      path++;
      idir[ss] = rp->fs_path.inode;
      pc_offs[ss] = (u8)(path - orig_path);
      ss++;
   }

   {
      const char *pc = orig_path + (sptr) pc_offs[ss - 1];
      ASSERT(path - pc > 0);
      get_entry(rp->fs, idir[ss-1], pc, path - pc, &rp->fs_path);
      rp->last_comp = pc;
   }
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
vfs_resolve(const char *path,
            vfs_path *rp,
            char *last_comp,
            bool exlock,
            bool res_last_sl)
{
   int rc;
   const char *fs_path;
   func_get_entry get_entry;
   char abs_path[MAX_PATH];
   task_info *curr = get_curr_task();

   kmutex_lock(&curr->pi->fslock);
   {
      rc = compute_abs_path(path, curr->pi->cwd, abs_path, MAX_PATH);
   }
   kmutex_unlock(&curr->pi->fslock);

   if (rc < 0)
      return rc;

   bzero(rp, sizeof(*rp));

   if (!(rp->fs = get_retained_fs_at(abs_path, &fs_path)))
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

   if (last_comp) {
      memcpy(last_comp, rp->last_comp, strlen(rp->last_comp)+1);
      rp->last_comp = last_comp;
   } else {
      rp->last_comp = NULL;
   }

   return rc;
}
