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

static inline const char *
__vfs_res_handle_dot_slash(const char *path)
{
   /* Handle the case of multiple slashes. */
   while (*path == '/')
      path++;

   /* Handle the case of a single '.' */
   if (*path == '.') {

      if (!path[1]) {

         /* case: /a/b/c/. */
         path++;

      } else if (path[1] == '/') {

         /* case: /a/b/c/./ */
         path += 2;   /* just skip the "/." substring */
      }
   }

   return path;
}

static inline void __vfs_smart_fs_lock(filesystem *fs, bool exlock)
{
   /* See the comment in vfs.h about the "fs-lock" funcs */
   exlock ? vfs_fs_exlock(fs) : vfs_fs_shlock(fs);
}

static inline void __vfs_smart_fs_unlock(filesystem *fs, bool exlock)
{
   /* See the comment in vfs.h about the "fs-lock" funcs */
   exlock ? vfs_fs_exunlock(fs) : vfs_fs_shunlock(fs);
}

typedef struct {

   vfs_path *rp;                /* pointer to the out-parameter vfs_path */
   const char *orig_path;       /* original path (used for offsets) */
   fs_path_struct orig_fs_path; /* original value of rp->fs_path */
   uptr ss;                     /* stack size */
   bool exlock;                 /* true -> exlock, false -> shlock */
   vfs_inode_ptr_t idir[32];    /* directory inodes */
   filesystem *fss[32];         /* filesystems */
   u8 pc_offs[32];              /* path component offset (from orig_path) */

} vfs_resolve_int_ctx;

static inline int
__vfs_resolve_stack_push(vfs_resolve_int_ctx *ctx,
                         const char *path,
                         filesystem *fs,
                         vfs_inode_ptr_t idir)
{
   if (ctx->ss == ARRAY_SIZE(ctx->idir))
      return -ENAMETOOLONG;

   ctx->pc_offs[ctx->ss] = (u8) (path - ctx->orig_path);
   ctx->idir[ctx->ss] = idir;
   ctx->fss[ctx->ss] = fs;
   ctx->ss++;
   return 0;
}

static inline void
__vfs_resolve_get_entry(vfs_resolve_int_ctx *ctx, const char *path)
{
   const uptr ss = ctx->ss;
   vfs_path *const rp = ctx->rp;
   filesystem *const fs = rp->fs;
   const char *const pc = ctx->orig_path + (sptr) ctx->pc_offs[ss - 1];

   ASSERT(path - pc > 0);
   fs->fsops->get_entry(fs, ctx->idir[ss - 1], pc, path - pc, &rp->fs_path);
   rp->last_comp = pc;

   filesystem *target_fs = mp2_get_retained_at(rp->fs, rp->fs_path.inode);

   if (target_fs) {

      /* unlock and release the current (host) filesystem */
      __vfs_smart_fs_unlock(rp->fs, ctx->exlock);
      release_obj(rp->fs);

      /* lock the new (target) filesystem. NOTE: it's already retained */
      __vfs_smart_fs_lock(target_fs, ctx->exlock);

      rp->fs = target_fs;

      /* Get root's entry */
      target_fs->fsops->get_entry(target_fs, NULL, NULL, 0, &rp->fs_path);
   }
}

static inline bool
__vfs_res_hit_dot_dot(const char *path)
{
   return path[0] == '.' && path[1] == '.' && (!path[2] || path[2] == '/');
}

static inline bool
__vfs_res_does_path_end_here(const char *path)
{
   return !path[0] || (path[0] == '/' && !path[1]);
}

/*
 * Returns `true` if the caller has to return 0, or `false` if the caller should
 * continue the loop.
 */
STATIC bool
__vfs_res_handle_dot_dot(vfs_resolve_int_ctx *ctx,
                         const char **path_ref)
{
   vfs_path *const rp = ctx->rp;
   filesystem *const fs = rp->fs;
   const char *const orig_path = ctx->orig_path;

   if (!__vfs_res_hit_dot_dot(*path_ref))
      return false;

   if (ctx->ss > 2) {

      const uptr ss = --ctx->ss;
      const char *pc = orig_path + (sptr) ctx->pc_offs[ss - 1];
      const char *old = orig_path + (sptr) ctx->pc_offs[ss - 2];
      fs->fsops->get_entry(fs, ctx->idir[ss-2], old, pc-old-1, &rp->fs_path);
      rp->last_comp = old;

   } else {

      rp->fs_path = ctx->orig_fs_path;

      if (ctx->ss > 1)
         ctx->ss--;
   }

   *path_ref += 2;
   return true;
}

static inline bool
__vfs_resolve_have_to_return(vfs_resolve_int_ctx *ctx,
                             const char *path,
                             int *rc)
{
   vfs_path *const rp = ctx->rp;

   if (!rp->fs_path.inode) {

      *rc = path[1]
         ? -ENOENT /* the path does NOT end here: no such entity */
         : 0;      /* the path just ends with a trailing slash */

      return true;
   }

   /* We've found an entity for this path component (pc) */

   if (!path[1]) {

      /* The path ends here, with a trailing slash */
      *rc = rp->fs_path.type != VFS_DIR
         ? -ENOTDIR /* if the entry is not a dir, that's a problem */
         : 0;

      return true;
   }

   return false;
}

static inline bool
__vfs_res_handle_trivial_path(vfs_resolve_int_ctx *ctx,
                              const char **path_ref,
                              int *rc)
{
   ctx->rp->last_comp = *path_ref;

   if (!**path_ref) {
      *rc = -ENOENT;
      return true;
   }

   *path_ref = __vfs_res_handle_dot_slash(*path_ref);

   if (!**path_ref) {
      /* path was just "/" */
      ctx->rp->last_comp = *path_ref;
      *rc = 0;
      return true;
   }

   return false;
}

STATIC int
__vfs_resolve(const char *path,
              vfs_path *rp,
              bool exlock,
              bool res_last_sl)
{
   int rc;
   vfs_resolve_int_ctx ctx = {
      .rp = rp,
      .orig_path = path,
      .orig_fs_path = rp->fs_path,
      .ss = 0,
      .exlock = exlock,
   };

   /* the vfs_path `rp` is assumed to be valid */
   ASSERT(rp->fs != NULL);
   ASSERT(rp->fs_path.inode != NULL);

   if (__vfs_res_handle_trivial_path(&ctx, &path, &rc))
      return rc;

   __vfs_resolve_stack_push(&ctx, path, rp->fs, rp->fs_path.inode);

   for (; *path; path++) {

      if (__vfs_res_handle_dot_dot(&ctx, &path)) {

         if (__vfs_res_does_path_end_here(path))
            return 0;

         continue;
      }

      if (*path != '/')
         continue;

      /* ------- we hit a slash in path: handle the component ------- */
      __vfs_resolve_get_entry(&ctx, path);

      path = __vfs_res_handle_dot_slash(path + 1) - 1;

      if (__vfs_resolve_have_to_return(&ctx, path, &rc))
         return rc;

      __vfs_resolve_stack_push(&ctx, path + 1, rp->fs, rp->fs_path.inode);
   }

   /* path ended without '/': we have to resolve the last component now */
   __vfs_resolve_get_entry(&ctx, path);
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
   char abs_path[MAX_PATH];
   process_info *pi = get_curr_task()->pi;
   bzero(rp, sizeof(*rp));

   if (*path != '/') {

      kmutex_lock(&pi->fslock);
      {
         rc = compute_abs_path(path, pi->cwd, abs_path, MAX_PATH);
      }
      kmutex_unlock(&pi->fslock);

      if (rc < 0)
         return rc;

   } else {

      rc = compute_abs_path(path, "/", abs_path, MAX_PATH);

      if (rc < 0)
         return rc;

      /* new (experimental) code */
      // rp->fs = mp2_get_root();
      // retain_obj(rp->fs);
      // fs_path = path;
   }

   if (!(rp->fs = get_retained_fs_at(abs_path, &fs_path)))
      return -ENOENT;

   __vfs_smart_fs_lock(rp->fs, exlock);

   /* Get root's entry */
   rp->fs->fsops->get_entry(rp->fs, NULL, NULL, 0, &rp->fs_path);
   rc = __vfs_resolve(fs_path, rp, exlock, res_last_sl);

   if (rc < 0) {
      /* resolve failed: release the lock and the fs */
      __vfs_smart_fs_unlock(rp->fs, exlock);
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
