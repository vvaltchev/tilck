/* SPDX-License-Identifier: BSD-2-Clause */

static int ramfs_mkdir(vfs_path *p, mode_t mode)
{
   ramfs_path *rp = (ramfs_path *) &p->fs_path;
   ramfs_data *d = p->fs->device_data;
   ramfs_inode *new_dir;
   int rc;

   if (rp->inode)
      return -EEXIST;

   if (!(rp->dir_inode->mode & 0300)) /* write + execute */
      return -EACCES;

   if (!(new_dir = ramfs_create_inode_dir(d, mode, rp->dir_inode)))
      return -ENOSPC;

   if ((rc = ramfs_dir_add_entry(rp->dir_inode, p->last_comp, new_dir))) {
      ramfs_destroy_inode(d, new_dir);
      return rc;
   }

   return rc;
}

static int ramfs_rmdir(vfs_path *p)
{
   ramfs_path *rp = (ramfs_path *) &p->fs_path;
   ramfs_data *d = p->fs->device_data;
   ramfs_inode *i = rp->inode;

   ASSERT(rwlock_wp_holding_exlock(&d->rwlock));

   if (rp->type != VFS_DIR)
      return -ENOTDIR;

   if (!(rp->dir_inode->mode & 0200)) /* write permission */
      return -EACCES;

   if (!rp->dir_entry)
      return -EINVAL; /* root dir case */

   if (p->last_comp[0] == '.' && !p->last_comp[1])
      return -EINVAL; /* trying to delete /a/b/c/. */

   if (i->num_entries > 2)
      return -ENOTEMPTY; /* empty dirs have two entries: '.' and '..' */

   ASSERT(i->entries_tree_root != NULL);
   ramfs_dir_remove_entry(i, i->entries_tree_root);   // drop .

   ASSERT(i->entries_tree_root != NULL);
   ramfs_dir_remove_entry(i, i->entries_tree_root);   // drop ..

   ASSERT(i->num_entries == 0);
   ASSERT(i->entries_tree_root == NULL);

   /* Remove the dir entry */
   ramfs_dir_remove_entry(rp->dir_inode, rp->dir_entry);

   /* Destroy the inode */
   ramfs_destroy_inode(d, i);
   return 0;
}
