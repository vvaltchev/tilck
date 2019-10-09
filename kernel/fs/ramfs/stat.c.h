/* SPDX-License-Identifier: BSD-2-Clause */

static int
ramfs_stat(filesystem *fs, vfs_inode_ptr_t i, struct stat64 *statbuf)
{
   if (!i)
      return -ENOENT;

   ramfs_inode *inode = i;

   if (!(inode->parent_dir->mode & 0500)) /* read + execute */
      return -EACCES;

   bzero(statbuf, sizeof(struct stat64));

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
            (inode->num_entries * (offt) sizeof(ramfs_entry));
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

   statbuf->st_ctim.tv_sec = inode->ctime;
   statbuf->st_mtim.tv_sec = inode->mtime;
   statbuf->st_atim = statbuf->st_mtim;

   return 0;
}
