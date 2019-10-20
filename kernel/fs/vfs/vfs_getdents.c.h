/* SPDX-License-Identifier: BSD-2-Clause */

typedef struct {

   fs_handle_base *h;
   struct linux_dirent64 *user_dirp;
   u32 buf_size;
   u32 offset;
   u32 fs_flags;
   offt off;
   struct linux_dirent64 ent;

} vfs_getdents_ctx;

static inline unsigned char
vfs_type_to_linux_dirent_type(enum vfs_entry_type t)
{
   static const unsigned char table[] =
   {
      [VFS_NONE]        = DT_UNKNOWN,
      [VFS_FILE]        = DT_REG,
      [VFS_DIR]         = DT_DIR,
      [VFS_SYMLINK]     = DT_LNK,
      [VFS_CHAR_DEV]    = DT_CHR,
      [VFS_BLOCK_DEV]   = DT_BLK,
      [VFS_PIPE]        = DT_FIFO,
   };

   ASSERT(t != VFS_NONE);
   return table[t];
}

static int vfs_getdents_cb(vfs_dent64 *vde, void *arg)
{
   const u16 entry_size = sizeof(struct linux_dirent64) + vde->name_len;
   struct linux_dirent64 *user_ent;
   vfs_getdents_ctx *ctx = arg;

   if (ctx->fs_flags & VFS_FS_RQ_DE_SKIP) {

      /*
       * Pseudo-hack used fortunately *only* by the FAT32 struct fs: it
       * implements here in the VFS layer a trivial mechanism to skip the first
       * `pos` entries of a directory in case the struct fs does not have a
       * way to just "save" the current position and resume from there in each
       * call of the fs-op getdents().
       *
       * TODO: implement in FAT32 a way to walk a directory step-by-step,
       * instead of having to walk all the entries every time and skipping the
       * already "returned" entries. This way we could drop VFS_FS_RQ_DE_SKIP.
       */

      if (ctx->off < ctx->h->pos) {
         ctx->off++;
         return 0; /* skip the dentry */
      }
   }

   if (ctx->offset + entry_size > ctx->buf_size) {

      if (!ctx->offset) {

         /*
          * We haven't "returned" any entries yet and the buffer is too small
          * for our first entry.
          */

         return -EINVAL;
      }

      /* We "returned" at least one entry */
      return (int) ctx->offset;
   }

   ctx->ent.d_ino    = vde->ino;
   ctx->ent.d_off    = (u64) ctx->off + 1; /* "offset" (=ID) of the next dent */
   ctx->ent.d_reclen = entry_size;
   ctx->ent.d_type   = vfs_type_to_linux_dirent_type(vde->type);

   user_ent = (void *)((char *)ctx->user_dirp + ctx->offset);

   if (copy_to_user(user_ent, &ctx->ent, sizeof(ctx->ent)) < 0)
      return -EFAULT;

   if (copy_to_user(user_ent->d_name, vde->name, vde->name_len) < 0)
      return -EFAULT;

   ctx->offset += entry_size;
   ctx->off++;
   ctx->h->pos++;
   return 0;
}

int vfs_getdents64(fs_handle h, struct linux_dirent64 *user_dirp, u32 buf_size)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   fs_handle_base *hb = (fs_handle_base *) h;
   int rc;

   ASSERT(hb != NULL);
   ASSERT(hb->fs->fsops->getdents);

   vfs_getdents_ctx ctx = {
      .h             = hb,
      .user_dirp     = user_dirp,
      .buf_size      = buf_size,
      .offset        = 0,
      .fs_flags      = hb->fs->flags,
      .off           = hb->fs->flags & VFS_FS_RQ_DE_SKIP ? 0 : ctx.h->pos,
      .ent           = { 0 },
   };

   /* See the comment in vfs.h about the "fs-locks" */
   vfs_fs_shlock(hb->fs);
   {
      rc = hb->fs->fsops->getdents(hb, &vfs_getdents_cb, &ctx);

      if (!rc)
         rc = (int) ctx.offset;
   }
   vfs_fs_shunlock(hb->fs);
   return rc;
}
