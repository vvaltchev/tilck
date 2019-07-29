/* SPDX-License-Identifier: BSD-2-Clause */

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

static inline void
vfs_resolve_stack_pop(vfs_resolve_int_ctx *ctx)
{
   ASSERT(ctx->ss > 0);
   ctx->ss--;
}

static inline vfs_path *
vfs_resolve_stack_top(vfs_resolve_int_ctx *ctx)
{
   return &ctx->paths[ctx->ss - 1];
}

static int
vfs_resolve_stack_push(vfs_resolve_int_ctx *ctx,
                       const char *path,
                       vfs_path *p)
{
   const int ss = ctx->ss;

   if (ss == ARRAY_SIZE(ctx->paths))
      return -ELOOP;

   if (p->fs_path.inode)
      vfs_retain_inode_at(p);

   ctx->orig_paths[ss] = path;
   ctx->paths[ss] = *p;
   ctx->paths[ss].last_comp = path;
   ctx->ss++;
   return 0;
}

static int
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
__vfs_resolve_get_entry(vfs_inode_ptr_t idir,
                        const char *pc,
                        const char *path,
                        vfs_path *rp,
                        bool exlock)
{
   DEBUG_VALIDATE_STACK_PTR();

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

static void get_locked_and_retained_root(vfs_path *rp, bool exlock)
{
   rp->fs = mp2_get_root();
   retain_obj(rp->fs);
   vfs_smart_fs_lock(rp->fs, exlock);
   vfs_get_root_entry(rp->fs, &rp->fs_path);
}

/* See the code below */
static int __vfs_resolve(vfs_resolve_int_ctx *ctx, bool res_last_sl);

static int
vfs_resolve_symlink(vfs_resolve_int_ctx *ctx, vfs_path *np)
{
   int rc;
   const char *lc = np->last_comp;
   char *symlink = ctx->sym_paths[ctx->ss - 1];
   vfs_path *rp = vfs_resolve_stack_top(ctx);
   vfs_path np2;
   DEBUG_ONLY_UNSAFE(int saved_ss = ctx->ss);

   ASSERT(np->fs_path.type == VFS_SYMLINK);

   if ((rc = rp->fs->fsops->readlink(np, symlink)) < 0)
      return rc;

   /* readlink() does not zero-terminate the buffer */
   symlink[rc] = 0;

   if (*symlink == '/') {

      vfs_smart_fs_unlock(rp->fs, ctx->exlock);
      release_obj(rp->fs);

      rp = &np2;
      get_locked_and_retained_root(rp, ctx->exlock);
   }

   /* Push the current vfs path on the stack and call __vfs_resolve() */
   if ((rc = vfs_resolve_stack_push(ctx, symlink, rp)) < 0)
      return rc;

   rc = __vfs_resolve(ctx, true);

   /* At the end, resolve() must use exactly 1 stack frame */
   ASSERT(ctx->ss == saved_ss + 1);

   /*
    * Now replace our new path with stack's top (the result of __vfs_resolve).
    * Note: we have to override the value of `last_comp` with the older one
    * because its value does make any sense from the caller's point of view.
    * Example:
    *
    *    resolve('/a/b'), where 'b' is a symlink to /x/y/z.
    *
    * We expect the last_comp to be 'b', not 'z'.
    */
   *np = *vfs_resolve_stack_top(ctx);
   np->last_comp = lc;

   /*
    * __vfs_resolve() always retains the resolved inode, unless it returned a
    * value < 0 (error). It might seem unsafe to release the inode here as its
    * ref-count could drop to 0 until we get to stack_replace_top(), but
    * actually it's safe because we're holding *at least* a shared lock on
    * the FS owning this inode. What it matters is that we retained the inode
    * before releasing the lock (fs change case).
    */

   if (!rc && np->fs_path.inode)
      vfs_release_inode_at(np);

   vfs_resolve_stack_pop(ctx);
   return rc;
}

static int
vfs_resolve_get_entry(vfs_resolve_int_ctx *ctx,
                      const char *path,
                      vfs_path *np,
                      bool res_symlinks)
{
   vfs_path *rp = vfs_resolve_stack_top(ctx);
   *np = *rp;

   ASSERT(path - rp->last_comp > 0);
   __vfs_resolve_get_entry(rp->fs_path.inode,
                           rp->last_comp,
                           path,
                           np,
                           ctx->exlock);

   if (np->fs_path.type == VFS_SYMLINK && res_symlinks)
      return vfs_resolve_symlink(ctx, np);

   return 0;
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
static bool
vfs_resolve_handle_dot_dot(vfs_resolve_int_ctx *ctx,
                           const char **path_ref)
{
   if (!__vfs_res_hit_dot_dot(*path_ref))
      return false;

   vfs_path p = *vfs_resolve_stack_top(ctx);
   fs_path_struct root_fsp;
   const char *lc;

   *path_ref += 2;
   lc = **path_ref ? *path_ref + 1 : *path_ref;

   /* Get the root inode of the current file system */
   vfs_get_root_entry(p.fs, &root_fsp);

   if (root_fsp.inode != p.fs_path.inode) {

      /* in this fs, we can go further up */
      vfs_get_entry(p.fs, p.fs_path.inode, "..", 2, &p.fs_path);

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

   } else {
      /* there's nowhere to go further */
   }

   vfs_resolve_stack_replace_top(ctx, lc, &p);
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
vfs_res_handle_trivial_path(vfs_resolve_int_ctx *ctx,
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

static int
__vfs_resolve(vfs_resolve_int_ctx *ctx, bool res_last_sl)
{
   int rc;
   const char *path = ctx->orig_paths[ctx->ss - 1];
   vfs_path *rp = vfs_resolve_stack_top(ctx);
   vfs_path np = *rp;
   DEBUG_VALIDATE_STACK_PTR();

   /* the vfs_path `rp` is assumed to be valid */
   ASSERT(rp->fs != NULL);
   ASSERT(rp->fs_path.inode != NULL);

   if (vfs_res_handle_trivial_path(ctx, &path, rp, &rc))
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
      if ((rc = vfs_resolve_get_entry(ctx, path, &np, true)))
         return rc;

      path = vfs_res_handle_dot_slash(path + 1) - 1;

      if (vfs_resolve_have_to_return(path, &np, &rc)) {

         if (rc)
            return rc;

         goto out;
      }

      vfs_resolve_stack_replace_top(ctx, path + 1, &np);
   }

   /* path ended without '/': we have to resolve the last component now */
   if ((rc = vfs_resolve_get_entry(ctx, path, &np, res_last_sl)))
      return rc;

out:
   return vfs_resolve_stack_replace_top(ctx, np.last_comp, &np);
}

static void
get_current_vfs_path(vfs_path *rp)
{
   process_info *pi = get_curr_task()->pi;

   kmutex_lock(&pi->fslock);
   {
      /* lazy set the default path to root */
      if (UNLIKELY(pi->cwd2.fs == NULL)) {
         vfs_path *tp = &pi->cwd2;
         tp->fs = mp2_get_root();
         vfs_get_root_entry(tp->fs, &tp->fs_path);
         retain_obj(tp->fs);
         vfs_retain_inode_at(tp);
      }

      /* Just copy `cwd2` into the address pointed by `rp` */
      *rp = pi->cwd2;
   }
   kmutex_unlock(&pi->fslock);
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
   bzero(rp, sizeof(*rp));

   vfs_resolve_int_ctx ctx = {
      .ss = 0,
      .exlock = exlock,
   };

   if (*path == '/') {

      get_locked_and_retained_root(rp, exlock);

   } else {

      get_current_vfs_path(rp);
      retain_obj(rp->fs);
      vfs_smart_fs_lock(rp->fs, exlock);
   }

   rc = vfs_resolve_stack_push(&ctx, path, rp);
   ASSERT(rc == 0);

   rc = __vfs_resolve(&ctx, res_last_sl);

   /* At the end, resolve() must use exactly 1 stack frame */
   ASSERT(ctx.ss == 1);

   /* Store out the last frame in the caller-provided vfs_path */
   *rp = ctx.paths[0];

   if (rp->fs_path.inode)
      vfs_release_inode_at(rp);

   if (rc < 0) {
      /* resolve failed: release the lock and the fs */
      vfs_smart_fs_unlock(rp->fs, exlock);
      release_obj(rp->fs);
   }

   return rc;
}
