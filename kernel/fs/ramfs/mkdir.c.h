/* SPDX-License-Identifier: BSD-2-Clause */

static int ramfs_mkdir(filesystem *fs, const char *path, mode_t mode)
{
   ramfs_data *d = fs->device_data;
   ramfs_resolved_path rp;
   ramfs_inode *new_dir;
   int rc;

   if ((rc = ramfs_resolve_path(d, path, &rp)))
      return rc;

   if (rp.i)
      return -EEXIST;

   if (!(rp.idir->mode & 0300)) /* write + execute */
      return -EACCES;

   if (!(new_dir = ramfs_create_inode_dir(d, mode, rp.idir)))
      return -ENOSPC;

   if ((rc = ramfs_dir_add_entry(rp.idir, rp.last_comp, new_dir))) {
      ramfs_destroy_inode(d, new_dir);
      return rc;
   }

   return rc;
}


static int ramfs_rmdir(filesystem *fs, const char *path)
{
   ramfs_data *d = fs->device_data;
   ramfs_resolved_path rp;
   int rc;

   ASSERT(rwlock_wp_holding_exlock(&d->rwlock));

   if ((rc = ramfs_resolve_path(d, path, &rp)))
      return rc;

   if (rp.i->type != RAMFS_DIRECTORY)
      return -ENOTDIR;

   if (!(rp.idir->mode & 0200)) /* write permission */
      return -EACCES;

   if (!rp.e)
      return -EINVAL; /* root directory case */

   if (rp.last_comp[0] == '.' && !rp.last_comp[1])
      return -EINVAL; /* trying to delete /a/b/c/. */

   if (rp.i->num_entries > 2)
      return -ENOTEMPTY; /* empty dirs have two entries: '.' and '..' */

   ASSERT(rp.i->entries_tree_root != NULL);
   ramfs_dir_remove_entry(rp.i, rp.i->entries_tree_root); /* drop '.' */

   ASSERT(rp.i->entries_tree_root != NULL);
   ramfs_dir_remove_entry(rp.i, rp.i->entries_tree_root); /* drop '..' */

   ASSERT(rp.i->num_entries == 0);
   ASSERT(rp.i->entries_tree_root == NULL);

   /* Remove the dir entry */
   ramfs_dir_remove_entry(rp.idir, rp.e);

   /* Destroy the inode */
   ramfs_destroy_inode(d, rp.i);
   return 0;
}
