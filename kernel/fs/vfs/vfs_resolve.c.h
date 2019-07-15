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
__vfs_resolve_stack_push(vfs_resolve_int_ctx *ctx,
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

static void
__vfs_resolve_get_entry_raw(vfs_inode_ptr_t idir,
                            const char *pc,
                            const char *path,
                            vfs_path *rp,
                            bool exlock)
{
   rp->fs->fsops->get_entry(rp->fs, idir, pc, path - pc, &rp->fs_path);
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
      target_fs->fsops->get_entry(target_fs, NULL, NULL, 0, &rp->fs_path);
   }
}

static void
__vfs_resolve_get_entry(vfs_resolve_int_ctx *ctx,
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
   if (!__vfs_res_hit_dot_dot(*path_ref))
      return false;

   if (ctx->ss > 1) {

      int ss = --ctx->ss;
      filesystem *old_fs = ctx->paths[ss].fs;
      filesystem *fs = ctx->paths[ss-1].fs;

      if (ss > 1 && ctx->paths[ss - 1].fs != ctx->paths[ss - 2].fs)
         for (int i = ss - 1; i >= 1; i--)
            ctx->paths[i].last_comp = ctx->paths[i-1].last_comp;

      if (fs != old_fs) {

         /* first, retain the new fs */
         retain_obj(fs);

         /* then, unlock and release the old fs */
         vfs_smart_fs_unlock(old_fs, ctx->exlock);
         release_obj(old_fs);

         /* finally, lock the new fs */
         vfs_smart_fs_lock(fs, ctx->exlock);
      }

   } else {

      /*
       * We can't get further than this using our stack (ctx->paths), but,
       * unless we're in the very root, get can go to the parent directory.
       *
       */

      vfs_path *p = &ctx->paths[ctx->ss - 1];
      fs_path_struct root_fsp;

      p->fs->fsops->get_entry(p->fs, NULL, NULL, 0, &root_fsp);

      if (root_fsp.inode != p->fs_path.inode) {

         /* in this very fs, we can go further up */
         p->fs->fsops->get_entry(p->fs, p->fs_path.inode, "..", 2, &p->fs_path);

      } else {

         if (p->fs != mp2_get_root()) {

            /* we have to go beyond the mount-point */
            int rc;
            mountpoint2 mp;

            rc = mp2_get_mountpoint_of(p->fs, &mp);
            ASSERT(rc == 0);
            ASSERT(mp.host_fs != p->fs);
            ASSERT(mp.target_fs == p->fs);
            ASSERT(mp.host_fs_inode != NULL);

            retain_obj(mp.host_fs);
            vfs_smart_fs_unlock(p->fs, ctx->exlock);
            vfs_smart_fs_lock(mp.host_fs, ctx->exlock);

            p->fs = mp.host_fs;
            p->fs->fsops->get_entry(p->fs,
                                    mp.host_fs_inode,
                                    "..",
                                    2,
                                    &p->fs_path);

            ASSERT(p->fs_path.inode != NULL);
            ASSERT(p->fs_path.type == VFS_DIR);

         } else {
            /* there's nowhere to go further */
         }
      }
   }

   *path_ref += 2;
   return true;
}

static inline bool
__vfs_resolve_have_to_return(const char *path, vfs_path *rp, int *rc)
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

   *path_ref = __vfs_res_handle_dot_slash(*path_ref);
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

      if (__vfs_res_handle_dot_dot(ctx, &path)) {

         if (__vfs_res_does_path_end_here(path))
            return 0;

         continue;
      }

      if (*path != '/')
         continue;

      /* ------- we hit a slash in path: handle the component ------- */
      __vfs_resolve_get_entry(ctx, path, &np);

      path = __vfs_res_handle_dot_slash(path + 1) - 1;

      if (__vfs_resolve_have_to_return(path, &np, &rc)) {

         if (rc)
            return rc;

         goto out;
      }

      __vfs_resolve_stack_push(ctx, path + 1, &np);
   }

   /* path ended without '/': we have to resolve the last component now */
   __vfs_resolve_get_entry(ctx, path, &np);

out:
   //printk("out: path is '%s'\n", path);
   rc = __vfs_resolve_stack_push(ctx, np.last_comp, &np);
   //printk("orig path: '%s'\n", ctx->orig_path);
   //printk("ret with last_comp: '%s'\n", np.last_comp);
   return rc;
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
            char *last_comp,
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
            tp->fs->fsops->get_entry(tp->fs, NULL, NULL, 0, &tp->fs_path);
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
      rp->fs->fsops->get_entry(rp->fs, NULL, NULL, 0, &rp->fs_path);
   }

   rc = __vfs_resolve_stack_push(&ctx, ctx.orig_path, rp);
   ASSERT(rc == 0);

   rc = __vfs_resolve(&ctx, res_last_sl);
   ASSERT(ctx.ss >= 1);

   if (rc < 0) {
      /* resolve failed: release the lock and the fs */
      filesystem *fs = ctx.paths[ctx.ss - 1].fs;
      vfs_smart_fs_unlock(fs, exlock);
      release_obj(fs); /* it was retained by get_retained_fs_at() */
   }

   *rp = ctx.paths[ctx.ss - 1];

   for (int i = ctx.ss - 1; i >= 0; i--) {
      if (ctx.paths[i].fs_path.inode)
         vfs_release_inode_at(&ctx.paths[i]);
   }

#ifndef KERNEL_TEST
   if (last_comp) {
      memcpy(last_comp, rp->last_comp, strlen(rp->last_comp)+1);
      rp->last_comp = last_comp;
   } else {
      rp->last_comp = NULL;
   }
#endif

   return rc;
}
