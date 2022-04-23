/* SPDX-License-Identifier: BSD-2-Clause */

static inline void vfs_smart_fs_lock(struct mnt_fs *fs, bool exlock)
{
   /* See the comment in vfs.h about the "fs-lock" funcs */
   exlock ? vfs_fs_exlock(fs) : vfs_fs_shlock(fs);
}

static inline void vfs_smart_fs_unlock(struct mnt_fs *fs, bool exlock)
{
   /* See the comment in vfs.h about the "fs-lock" funcs */
   exlock ? vfs_fs_exunlock(fs) : vfs_fs_shunlock(fs);
}

static inline void
vfs_resolve_stack_pop(struct vfs_resolve_int_ctx *ctx)
{
   ASSERT(ctx->ss > 0);
   ctx->ss--;
}

static inline struct vfs_path *
vfs_resolve_stack_top(struct vfs_resolve_int_ctx *ctx)
{
   return &ctx->paths[ctx->ss - 1];
}

static int
vfs_resolve_stack_push(struct vfs_resolve_int_ctx *ctx,
                       const char *path,
                       struct vfs_path *p)
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

static void
vfs_resolve_stack_replace_top(struct vfs_resolve_int_ctx *ctx,
                              const char *path,
                              struct vfs_path *p)
{
   struct vfs_path *cp;
   ASSERT(ctx->ss > 0);
   cp = &ctx->paths[--ctx->ss];

   if (cp->fs_path.inode)
      vfs_release_inode_at(cp);

   /* push cannot fail here because we've just dropped one frame */
   vfs_resolve_stack_push(ctx, path, p);
}

static void
__vfs_resolve_get_entry(vfs_inode_ptr_t idir,
                        const char *pc,
                        const char *path,
                        struct vfs_path *rp,
                        bool exlock)
{
   vfs_get_entry(rp->fs, idir, pc, path - pc, &rp->fs_path);
   rp->last_comp = pc;

   struct mnt_fs *target_fs = mp_get_retained_at(rp->fs, rp->fs_path.inode);

   if (target_fs) {

      /* unlock and release the current (host) struct mnt_fs */
      vfs_smart_fs_unlock(rp->fs, exlock);
      release_obj(rp->fs);

      rp->fs = target_fs;
      /* lock the new (target) struct mnt_fs. NOTE: it's already retained */
      vfs_smart_fs_lock(rp->fs, exlock);

      /* Get root's entry */
      vfs_get_root_entry(target_fs, &rp->fs_path);
   }
}

static void get_locked_retained_root(struct vfs_path *rp, bool exlock)
{
   rp->fs = mp_get_root();
   retain_obj(rp->fs);
   vfs_smart_fs_lock(rp->fs, exlock);
   vfs_get_root_entry(rp->fs, &rp->fs_path);
}

/* See the code below */
static int __vfs_resolve(struct vfs_resolve_int_ctx *ctx, bool res_last_sl);

static int
vfs_resolve_symlink(struct vfs_resolve_int_ctx *ctx, struct vfs_path *np)
{
   int rc;
   const char *lc = np->last_comp;
   char *symlink = ctx->sym_paths[ctx->ss - 1];
   struct vfs_path *rp = vfs_resolve_stack_top(ctx);
   struct vfs_path np2;
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
      get_locked_retained_root(rp, ctx->exlock);
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
    * __vfs_resolve() always retains the resolved inode. It might seem unsafe
    * to release the inode here as its ref-count could drop to 0 until we get
    * to stack_replace_top(), but actually it's safe because we're holding
    * *at least* a shared lock on the FS owning this inode. What it matters is
    * that we retained the inode before releasing the lock (fs change case).
    */

   if (np->fs_path.inode)
      vfs_release_inode_at(np);

   if (np->fs && np->fs != rp->fs)
      vfs_smart_fs_unlock(np->fs, ctx->exlock);

   vfs_resolve_stack_pop(ctx);
   return rc;
}

static inline bool
vfs_is_path_dotdot(const char *path)
{
   return path[0] == '.' && path[1] == '.' && slash_or_nul(path[2]);
}

/* Returns true if the function completely handled the current component */
static bool
vfs_handle_cross_fs_dotdot(struct vfs_resolve_int_ctx *ctx,
                           const char *path,
                           struct vfs_path *np)
{
   struct fs_path root_fsp;
   struct mountpoint *mp;

   if (!vfs_is_path_dotdot(path))
      return false;

   *np = *vfs_resolve_stack_top(ctx);

   /* Get the root inode of the current file system */
   vfs_get_root_entry(np->fs, &root_fsp);

   if (root_fsp.inode != np->fs_path.inode)
      return false; /* we can go further up: no need for special handling */

   if (np->fs == mp_get_root())
      return true; /* there's nowhere to go further */

   /*
    * Here we've at the root of a FS mounted somewhere other than the absolute
    * root. Going to '..' from here means going beyond its mount-point.
    */
   mp = mp_get_retained_mp_of(np->fs);

   ASSERT(mp != NULL);
   ASSERT(mp->host_fs != np->fs);
   ASSERT(mp->target_fs == np->fs);
   ASSERT(mp->host_fs_inode != NULL);

   /*
    * Here it's tricky: we have to switch the FS we're using for the
    * resolving. Current state:
    *
    *    1. The current FS is pointed by np->fs
    *    2. The current FS is locked (exlock or shlock)
    *    3. The current FS is retained
    *    4. The current dir in the current FS is retained
    *    5. The mountpoint where the current FS is retained
    *
    * Therefore, we need to (the order matters):
    *
    *    1. retain the new FS
    *    2. unlock the current FS
    *    3. lock the new FS
    *    4. release the current FS (now unlocked)
    *    5. release the mountpoint
    *
    * NOTE: the current dir in the current FS won't be released here. That
    * will happen in vfs_resolve_stack_replace_top().
    */
   retain_obj(mp->host_fs);
   vfs_smart_fs_unlock(np->fs, ctx->exlock);
   vfs_smart_fs_lock(mp->host_fs, ctx->exlock);
   release_obj(np->fs);
   release_obj(mp);

   np->fs = mp->host_fs;
   vfs_get_entry(np->fs, mp->host_fs_inode, path, 2, &np->fs_path);

   ASSERT(np->fs_path.inode != NULL);
   ASSERT(np->fs_path.type == VFS_DIR);
   return true;
}

static int
vfs_resolve_get_entry(struct vfs_resolve_int_ctx *ctx,
                      const char *path,
                      struct vfs_path *np,
                      bool res_symlinks)
{
   struct vfs_path *rp = vfs_resolve_stack_top(ctx);
   const char *const lc = rp->last_comp;

   if (vfs_handle_cross_fs_dotdot(ctx, lc, np))
      return 0;

   *np = *rp;

   /*
    * If the path component is empty (e.g. in 2nd component in 'a//b'), just
    * return keeping the same VFS path.
    */
   if (lc == path)
      return 0;

   __vfs_resolve_get_entry(rp->fs_path.inode,
                           lc,
                           path,
                           np,
                           ctx->exlock);

   if (np->fs_path.type == VFS_SYMLINK && res_symlinks)
      return vfs_resolve_symlink(ctx, np);

   return 0;
}

static inline bool
vfs_resolve_have_to_stop(const char *path, struct vfs_path *rp, int *rc)
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

static int
__vfs_resolve(struct vfs_resolve_int_ctx *ctx, bool res_last_sl)
{
   int rc = 0;
   const char *path = ctx->orig_paths[ctx->ss - 1];
   struct vfs_path *rp = vfs_resolve_stack_top(ctx);
   struct vfs_path np = *rp;

   if (!*path)
      return -ENOENT;

   /* the struct vfs_path `rp` is assumed to be valid */
   ASSERT(rp->fs != NULL);
   ASSERT(rp->fs_path.inode != NULL);

   for (; *path; path++) {

      if (*path != '/')
         continue;

      if ((rc = vfs_resolve_get_entry(ctx, path, &np, true)))
         return rc;

      if (vfs_resolve_have_to_stop(path, &np, &rc))
         break;

      vfs_resolve_stack_replace_top(ctx, path + 1, &np);
   }

   if (*path == 0) {
      /* path ended without '/': we have to resolve the last component now */
      rc = vfs_resolve_get_entry(ctx, path, &np, res_last_sl);
   }

   vfs_resolve_stack_replace_top(ctx, np.last_comp, &np);
   return rc;
}

static void
get_locked_retained_cwd(struct vfs_path *rp, bool exlock)
{
   struct process *pi = get_curr_proc();

   kmutex_lock(&pi->fslock);
   {
      ASSERT(pi->cwd.fs != NULL);

      /* Just copy `cwd` into the address pointed by `rp` */
      *rp = pi->cwd;
      retain_obj(rp->fs);
   }
   kmutex_unlock(&pi->fslock);
   vfs_smart_fs_lock(rp->fs, exlock);
}

/*
 * Resolves the path, locking the last struct mnt_fs with an exclusive or a
 * shared lock depending on `exlock`. The last component of the path, if a
 * symlink, is resolved only with `res_last_sl` is true.
 *
 * NOTE: when the function succeedes (-> return 0), the struct mnt_fs is
 * returned as `rp->fs` RETAINED and LOCKED. The caller is supposed to first
 * release the right lock with vfs_shunlock() or with vfs_exunlock() and then
 * to release the FS with release_obj().
 */
int
vfs_resolve(const char *path,
            struct vfs_path *rp,
            bool exlock,
            bool res_last_sl)
{
   int rc;

#ifndef KERNEL_TEST

   struct task *curr = get_curr_task();
   struct vfs_resolve_int_ctx *ctx = (void *)curr->misc_buf->resolve_ctx;
   STATIC_ASSERT(sizeof(*ctx) <= sizeof(curr->misc_buf->resolve_ctx));

#else

   /* For the unit tests, just allocate the ctx on the stack */
   struct vfs_resolve_int_ctx __stack_allocated_ctx;
   struct vfs_resolve_int_ctx *ctx = &__stack_allocated_ctx;

#endif

   bzero(rp, sizeof(*rp));
   ctx->ss = 0;
   ctx->exlock = exlock;

   if (*path == '/')
      get_locked_retained_root(rp, exlock);
   else
      get_locked_retained_cwd(rp, exlock);

   rc = vfs_resolve_stack_push(ctx, path, rp);
   ASSERT(rc == 0);

   rc = __vfs_resolve(ctx, res_last_sl);

   /* At the end, resolve() must use exactly 1 stack frame */
   ASSERT(ctx->ss == 1);

   /* Store out the last frame in the caller-provided vfs_path */
   *rp = ctx->paths[0];

   if (rp->fs_path.inode)
      vfs_release_inode_at(rp);

   if (rc == 0) {

      /*
       * Resolve succeeded. Handle corner cases like 'a/b/c//'.
       * The last_comp must be 'c//' instead of '/', as returned by
       * __vfs_resolve().
       */
      if (*rp->last_comp == '/') {
         for (; rp->last_comp > path && *--rp->last_comp == '/'; ) { }
      }

   } else {

      /* resolve failed: release the lock and the fs */
      vfs_smart_fs_unlock(rp->fs, exlock);
      release_obj(rp->fs);
      bzero(rp, sizeof(struct vfs_path));
   }

   return rc;
}
