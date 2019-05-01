/* SPDX-License-Identifier: BSD-2-Clause */

static int ramfs_getdents(fs_handle h, get_dents_func_cb cb, void *arg)
{
   ramfs_handle *rh = h;
   ramfs_inode *inode = rh->inode;
   int rc = 0;

   if (inode->type != VFS_DIR)
      return -ENOTDIR;

   if (!(inode->mode & 0400)) /* read permission */
      return -EACCES;

   list_for_each_ro_kp(rh->dpos, &inode->entries_list, lnode) {

      vfs_dent64 dent = {
         .ino        = rh->dpos->inode->ino,
         .type       = rh->dpos->inode->type,
         .name_len   = rh->dpos->name_len,
         .name       = rh->dpos->name,
      };

      if ((rc = cb(&dent, arg)))
         break;
   }

   return rc;
}
