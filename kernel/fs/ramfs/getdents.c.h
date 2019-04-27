/* SPDX-License-Identifier: BSD-2-Clause */

typedef struct {

   get_dents_func_cb vfs_cb;
   void *vfs_ctx;

} ramfs_getdents_ctx_new;

static int ramfs_getdents_new_cb(void *obj, void *arg)
{
   ramfs_getdents_ctx_new *ctx = arg;
   ramfs_entry *pos = obj;

   vfs_dent64 dent = {
      .ino = pos->inode->inode,
      .type = pos->inode->type,
      .name = pos->name,
   };

   return ctx->vfs_cb(&dent, ctx->vfs_ctx);
}

static int ramfs_getdents_new(fs_handle h, get_dents_func_cb cb, void *arg)
{
   ramfs_handle *rh = h;
   ramfs_inode *inode = rh->inode;

   if (inode->type != VFS_DIR)
      return -ENOTDIR;

   if (!(inode->mode & 0400)) /* read permission */
      return -EACCES;

   ramfs_getdents_ctx_new ctx = {
      .vfs_cb = cb,
      .vfs_ctx = arg,
   };

   return bintree_in_order_visit(rh->inode->entries_tree_root,
                                 ramfs_getdents_new_cb,
                                 &ctx,
                                 ramfs_entry,
                                 node);
}
