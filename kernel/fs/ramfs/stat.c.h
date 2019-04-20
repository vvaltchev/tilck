/* SPDX-License-Identifier: BSD-2-Clause */

static int ramfs_fstat64(fs_handle h, struct stat64 *statbuf)
{
   if (!h)
      return -ENOENT;

   ramfs_handle *rh = h;
   ramfs_inode *inode = rh->inode;

   bzero(statbuf, sizeof(struct stat64));

   statbuf->st_dev = rh->fs->device_id;
   statbuf->st_ino = (typeof(statbuf->st_ino)) inode->inode;
   statbuf->st_mode = inode->mode;
   statbuf->st_nlink = inode->nlink;
   statbuf->st_uid = 0;  /* root */
   statbuf->st_gid = 0;  /* root */
   statbuf->st_rdev = 0; /* device ID if a special file: therefore, NO. */
   statbuf->st_size = (typeof(statbuf->st_size)) inode->fsize;
   statbuf->st_blksize = PAGE_SIZE;
   statbuf->st_blocks =
      (typeof(statbuf->st_blocks)) (inode->blocks_count * (PAGE_SIZE / 512));

   statbuf->st_ctim.tv_sec = datetime_to_timestamp(inode->ctime);
   statbuf->st_mtim.tv_sec = datetime_to_timestamp(inode->wtime);
   statbuf->st_atim = statbuf->st_mtim;

   return 0;
}
