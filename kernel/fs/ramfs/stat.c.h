/* SPDX-License-Identifier: BSD-2-Clause */

static int
ramfs_stat_nolock(struct mnt_fs *fs,
                  struct ramfs_inode *inode,
                  struct k_stat64 *statbuf)
{
   ASSERT(inode);

   if ((inode->parent_dir->mode & 0500) != 0500) /* read + execute */
      return -EACCES;

   bzero(statbuf, sizeof(struct k_stat64));

   statbuf->st_dev = fs->device_id;
   statbuf->st_ino = inode->ino;
   statbuf->st_mode = inode->mode;
   statbuf->st_nlink = inode->nlink;
   statbuf->st_uid = 0;  /* root */
   statbuf->st_gid = 0;  /* root */
   statbuf->st_rdev = 0; /* device ID if a special file: therefore, NO. */

   switch (inode->type) {

      case VFS_FILE:
         statbuf->st_size = (typeof(statbuf->st_size)) inode->fsize;
         break;

      case VFS_DIR:
         statbuf->st_size = (typeof(statbuf->st_size))
            (inode->num_entries * (offt) sizeof(struct ramfs_entry));
         break;

      case VFS_SYMLINK:
         statbuf->st_size = (typeof(statbuf->st_size)) inode->path_len;
         break;

      default:
         NOT_IMPLEMENTED();
         break;
   }

   statbuf->st_blksize = PAGE_SIZE;
   statbuf->st_blocks =
      (typeof(statbuf->st_blocks)) (inode->blocks_count * (PAGE_SIZE / 512));

   statbuf->st_ctim = to_stat_timespec(inode->ctime);
   statbuf->st_mtim = to_stat_timespec(inode->mtime);
   statbuf->st_atim = to_stat_timespec(inode->mtime);
   return 0;
}

static int
ramfs_stat(struct mnt_fs *fs, vfs_inode_ptr_t i, struct k_stat64 *statbuf)
{
   struct ramfs_inode *inode = i;
   int rc;

   if (!inode)
      return -ENOENT;

   rwlock_wp_shlock(&inode->rwlock);
   {
      rc = ramfs_stat_nolock(fs, inode, statbuf);
   }
   rwlock_wp_shunlock(&inode->rwlock);
   return rc;
}
