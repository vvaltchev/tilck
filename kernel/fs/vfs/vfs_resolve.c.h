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
vfs_res_handle_dot_slash(const char *path)
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

static inline void vfs_smart_fs_lock(filesystem *fs, bool exlock)
{
   /* See the comment in vfs.h about the "fs-lock" funcs */
   exlock ? vfs_fs_exlock(fs) : vfs_fs_shlock(fs);
}

static inline void vfs_smart_fs_unlock(filesystem *fs, bool exlock)
{
   /* See the comment in vfs.h about the "fs-lock" funcs */
   exlock ? vfs_fs_exunlock(fs) : vfs_fs_shunlock(fs);
}

STATIC int
vfs_resolve_stack_push(vfs_resolve_int_ctx *ctx,
                         const char *path,
                         vfs_path *p)
{
   if (ctx->ss == ARRAY_SIZE(ctx->paths))
      return -ENAMETOOLONG;

   if (p->fs_path.inode)
      vfs_retain_inode_at(p);

   ctx->paths[ctx->ss] = *p;
   ctx->paths[ctx->ss].last_comp = path;
   ctx->ss++;
   return 0;
}

STATIC int
vfs_resolve_stack_replace_top(vfs_resolve_int_ctx *ctx,
                                const char *path,
                                vfs_path *p)
{
   vfs_path *cp;
   ASSERT(ctx->ss > 0);
   cp = &ctx->paths[--ctx->ss];

   if (cp->fs_path.inode)
      vfs_release_inode_at(cp);

   return vfs_resolve_stack_push(ctx, path, p);
}

static void
__vfs_resolve_get_entry_raw(vfs_inode_ptr_t idir,
                            const char *pc,
                            const char *path,
                            vfs_path *rp,
                            bool exlock)
{
   vfs_get_entry(rp->fs, idir, pc, path - pc, &rp->fs_path);
   rp->last_comp = pc;

   filesystem *target_fs = mp2_get_retained_at(rp->fs, rp->fs_path.inode);

   if (target_fs) {

      /* unlock and release the current (host) filesystem */
      vfs_smart_fs_unlock(rp->fs, exlock);
      release_obj(rp->fs);

      rp->fs = target_fs;
      /* lock the new (target) filesystem. NOTE: it's already retained */
      vfs_smart_fs_lock(rp->fs, exlock);

      /* Get root's entry */
      vfs_get_root_entry(target_fs, &rp->fs_path);
   }
}

static void
vfs_resolve_get_entry(vfs_resolve_int_ctx *ctx,
                        const char *path,
                        vfs_path *np)
{
   const int ss = ctx->ss;
   vfs_path *rp = &ctx->paths[ss-1];
   *np = *rp;

   ASSERT(path - ctx->paths[ss-1].last_comp > 0);
   __vfs_resolve_get_entry_raw(ctx->paths[ss-1].fs_path.inode,
                               ctx->paths[ss-1].last_comp,
                               path,
                               np,
                               ctx->exlock);
}

static inline bool
__vfs_res_hit_dot_dot(const char *path)
{
   return path[0] == '.' && path[1] == '.' && (!path[2] || path[2] == '/');
}

static inline bool
vfs_res_does_path_end_here(const char *path)
{
   return !path[0] || (path[0] == '/' && !path[1]);
}

/*
 * Returns `true` if the caller has to return 0, or `false` if the caller should
 * continue the loop.
 */
STATIC bool
vfs_resolve_handle_dot_dot(vfs_resolve_int_ctx *ctx,
                           const char **path_ref)
{
   if (!__vfs_res_hit_dot_dot(*path_ref))
      return false;

   vfs_path p = ctx->paths[ctx->ss - 1];
   fs_path_struct root_fsp;
   const char *lc;

   *path_ref += 2;
   lc = **path_ref ? *path_ref + 1 : *path_ref;

   /* Get the root inode of the current file system */
   vfs_get_root_entry(p.fs, &root_fsp);

   if (root_fsp.inode != p.fs_path.inode) {

      /* in this fs, we can go further up */
      vfs_get_entry(p.fs, p.fs_path.inode, "..", 2, &p.fs_path);
      vfs_resolve_stack_replace_top(ctx, lc, &p);

   } else if (p.fs != mp2_get_root()) {

      /* we have to go beyond the mount-point */
      int rc;
      mountpoint2 mp;

      rc = mp2_get_mountpoint_of(p.fs, &mp);
      ASSERT(rc == 0);
      ASSERT(mp.host_fs != p.fs);
      ASSERT(mp.target_fs == p.fs);
      ASSERT(mp.host_fs_inode != NULL);

      retain_obj(mp.host_fs);
      vfs_smart_fs_unlock(p.fs, ctx->exlock);
      vfs_smart_fs_lock(mp.host_fs, ctx->exlock);

      p.fs = mp.host_fs;
      vfs_get_entry(p.fs, mp.host_fs_inode, "..", 2, &p.fs_path);

      ASSERT(p.fs_path.inode != NULL);
      ASSERT(p.fs_path.type == VFS_DIR);

      vfs_resolve_stack_replace_top(ctx, lc, &p);

   } else {
      /* there's nowhere to go further */
   }

   return true;
}

static inline bool
vfs_resolve_have_to_return(const char *path, vfs_path *rp, int *rc)
{
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
                              vfs_path *rp,
                              int *rc)
{
   rp->last_comp = *path_ref;

   if (!**path_ref) {
      *rc = -ENOENT;
      return true;
   }

   *path_ref = vfs_res_handle_dot_slash(*path_ref);
   rp->last_comp = *path_ref;

   if (!**path_ref) {
      /* path was just "/" */
      *rc = 0;
      return true;
   }

   return false;
}

STATIC int
__vfs_resolve(vfs_resolve_int_ctx *ctx, bool res_last_sl)
{
   int rc;
   const char *path = ctx->orig_path;
   vfs_path *rp = &ctx->paths[ctx->ss-1];
   vfs_path np = *rp;

   /* the vfs_path `rp` is assumed to be valid */
   ASSERT(rp->fs != NULL);
   ASSERT(rp->fs_path.inode != NULL);

   if (__vfs_res_handle_trivial_path(ctx, &path, rp, &rc))
      return rc;

   for (; *path; path++) {

      if (vfs_resolve_handle_dot_dot(ctx, &path)) {

         if (vfs_res_does_path_end_here(path))
            return 0;

         continue;
      }

      if (*path != '/')
         continue;

      /* ------- we hit a slash in path: handle the component ------- */
      vfs_resolve_get_entry(ctx, path, &np);

      path = vfs_res_handle_dot_slash(path + 1) - 1;

      if (vfs_resolve_have_to_return(path, &np, &rc)) {

         if (rc)
            return rc;

         goto out;
      }

      vfs_resolve_stack_replace_top(ctx, path + 1, &np);
   }

   /* path ended without '/': we have to resolve the last component now */
   vfs_resolve_get_entry(ctx, path, &np);

out:
   return vfs_resolve_stack_replace_top(ctx, np.last_comp, &np);
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
int
vfs_resolve(const char *path,
            vfs_path *rp,
            bool exlock,
            bool res_last_sl)
{
   int rc;
   process_info *pi = get_curr_task()->pi;
   bzero(rp, sizeof(*rp));

   vfs_resolve_int_ctx ctx = {
      .orig_path = path,
      .ss = 0,
      .exlock = exlock,
   };

   if (*path != '/') {

      kmutex_lock(&pi->fslock);
      {
         if (!pi->cwd2.fs) {
            vfs_path *tp = &pi->cwd2;
            tp->fs = mp2_get_root();
            vfs_get_root_entry(tp->fs, &tp->fs_path);
            // TODO: retain fs/inode ?
         }

         *rp = pi->cwd2;
      }
      kmutex_unlock(&pi->fslock);

      retain_obj(rp->fs);
      vfs_smart_fs_lock(rp->fs, exlock);

   } else {

      rp->fs = mp2_get_root();
      retain_obj(rp->fs);
      vfs_smart_fs_lock(rp->fs, exlock);

      /* Get root's entry */
      vfs_get_root_entry(rp->fs, &rp->fs_path);
   }

   rc = vfs_resolve_stack_push(&ctx, ctx.orig_path, rp);
   ASSERT(rc == 0);

   rc = __vfs_resolve(&ctx, res_last_sl);
   ASSERT(ctx.ss >= 1);

   if (rc < 0) {
      /* resolve failed: release the lock and the fs */
      filesystem *fs = ctx.paths[ctx.ss - 1].fs;
      vfs_smart_fs_unlock(fs, exlock);
      release_obj(fs);
   }

   *rp = ctx.paths[ctx.ss - 1];

   for (int i = ctx.ss - 1; i >= 0; i--) {
      if (ctx.paths[i].fs_path.inode)
         vfs_release_inode_at(&ctx.paths[i]);
   }

   return rc;
}
