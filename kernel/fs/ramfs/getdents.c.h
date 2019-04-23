/* SPDX-License-Identifier: BSD-2-Clause */

typedef struct {

   ramfs_handle *rh;
   struct linux_dirent64 *dirp;
   u32 buf_size;
   u32 offset;
   int curr_index;
   struct linux_dirent64 ent;

} ramfs_getdents_ctx;

static int ramfs_getdents64_cb(void *obj, void *arg)
{
   ramfs_getdents_ctx *ctx = arg;
   ramfs_entry *pos = obj;

   if (ctx->curr_index < ctx->rh->pos) {
      ctx->curr_index++;
      return 0; // continue
   }

   const char *const file_name = pos->name;
   const u32 fl = (u32)strlen(file_name);
   const u32 entry_size = fl + 1 + sizeof(struct linux_dirent64);

   if (ctx->offset + entry_size > ctx->buf_size) {

      if (!ctx->offset) {

         /*
         * We haven't "returned" any entries yet and the buffer is too small
         * for our first entry.
         */

         return -EINVAL;
      }

      /* We "returned" at least one entry */
      return (int)ctx->offset;
   }

   ctx->ent.d_ino = (typeof(ctx->ent.d_ino)) pos->inode->inode;
   ctx->ent.d_off = (s64)(ctx->offset + entry_size);
   ctx->ent.d_reclen = (u16)entry_size;

   switch (pos->inode->type) {
      case RAMFS_DIRECTORY:
         ctx->ent.d_type = DT_DIR;
         break;
      case RAMFS_FILE:
         ctx->ent.d_type = DT_REG;
         break;
      case RAMFS_SYMLINK:
         ctx->ent.d_type = DT_LNK;
         break;
      default:
         ctx->ent.d_type = DT_UNKNOWN;
         break;
   }

   struct linux_dirent64 *user_ent = (void *)((char *)ctx->dirp + ctx->offset);

   if (copy_to_user(user_ent, &ctx->ent, sizeof(ctx->ent)) < 0)
      return -EFAULT;

   if (copy_to_user(user_ent->d_name, file_name, fl + 1) < 0)
      return -EFAULT;

   ctx->offset = (u32) ctx->ent.d_off; /* s64 to u32 precision drop */
   ctx->curr_index++;
   ctx->rh->pos++;
   return 0;
}

static int
ramfs_getdents64_int(ramfs_handle *rh,
                     struct linux_dirent64 *dirp,
                     u32 buf_size)
{
   ramfs_getdents_ctx ctx = {
      .rh = rh,
      .dirp = dirp,
      .buf_size = buf_size,
      .offset = 0,
      .curr_index = 0,
      .ent = { 0 },
   };

   int rc;

   rc = bintree_in_order_visit(rh->inode->entries_tree_root,
                               ramfs_getdents64_cb,
                               &ctx,
                               ramfs_entry,
                               node);

   if (rc)
      return rc;

   return (int) ctx.offset;
}

static int
ramfs_getdents64(fs_handle h, struct linux_dirent64 *dirp, u32 buf_size)
{
   int rc;
   ramfs_handle *rh = h;
   ramfs_inode *inode = rh->inode;

   if (inode->type != RAMFS_DIRECTORY)
      return -ENOTDIR;

   if (!(inode->mode & 0400)) /* read permission */
      return -EACCES;

   rwlock_wp_shlock(&inode->rwlock);
   {
      rc = ramfs_getdents64_int(rh, dirp, buf_size);
   }
   rwlock_wp_shunlock(&inode->rwlock);
   return rc;
}
