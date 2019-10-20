/* SPDX-License-Identifier: BSD-2-Clause */

static int ramfs_ioctl(fs_handle h, uptr cmd, void *argp)
{
   return -EINVAL;
}

static int ramfs_fcntl(fs_handle h, int cmd, int arg)
{
   return -EINVAL;
}

static offt ramfs_dir_seek(ramfs_handle *rh, offt target_off)
{
   ramfs_inode *i = rh->inode;
   ramfs_entry *dpos;
   offt off = 0;

   list_for_each_ro(dpos, &i->entries_list, lnode) {

      if (off == target_off) {
         rh->pos = off;
         rh->dpos = dpos;
         break;
      }

      off++;
   }

   return rh->pos;
}

static offt ramfs_seek(fs_handle h, offt off, int whence)
{
   ramfs_handle *rh = h;
   ramfs_inode *i = rh->inode;

   if (i->type == VFS_DIR) {

      if (whence != SEEK_SET || off < 0) {
         /*
          * Dirents offsets are NOT regular offsets having an actual scalar
          * value. They have to be treated as *opaque* values. Therefore, it
          * does not make ANY sense to accept values of `whence` other than
          * just SEEK_SET.
          */
         return -EINVAL;
      }

      return ramfs_dir_seek(rh, off);
   }

   switch (whence) {

      case SEEK_SET:
         rh->pos = off;
         break;

      case SEEK_CUR:
         rh->pos += off;
         break;

      case SEEK_END:
         rh->pos = (offt)i->fsize + off;
         break;

      default:
         return -EINVAL;
   }

   if (rh->pos < 0) {
      rh->pos = 0;
      return -EINVAL;
   }

   return rh->pos;
}

/*
 * ramfs_unmap_past_eof_mappings()
 *
 * While reducing the size of a file with truncate(), there could be processes
 * where the part of the file now becoming "past-EOF" is memory-mapped.
 * In order to be consistent with Linux, we have to un-map, from all the virtual
 * space of all of these processes, the "past-EOF" pages. This way, when the
 * processes try to access these pages, they'll receive a SIGBUS signal, exactly
 * the same way as if they mapped in memory content past EOF.
 *
 * How this is done
 * ----------------------
 *
 * Each inode has a `mappings_list` with all the user_mappings referring to it.
 * Assuming that `rlen` is the new length of the file after truncate, rounded-up
 * to PAGE_SIZE, for each `struct user_mapping` there are 3 cases:
 *
 * 1) The mapping remains is in a safe zone, even after the truncate() call:
 *
 *    0 KB        4 KB        8 KB        12 KB       16 KB       20 KB
 *    +-----------+-----------+-----------+-----------+-----------+-----------+
 *    |###########|###########|###########|###########|           |           |
 *    |           |  mapped   |  mapped   |           |           |           |
 *    +-----------+-----------+-----------+-----------+-----------+-----------+
 *                ^                       ^           ^
 *              um->off            um->off+um->len  rlen
 *
 * 2) The part of the mapping remains in a safe zone, part of it doesn't.
 *
 *    0 KB        4 KB        8 KB        12 KB       16 KB       20 KB
 *    +-----------+-----------+-----------+-----------+-----------+-----------+
 *    |###########|###########|           |           |           |           |
 *    |           |  mapped   |  mapped   |  mapped   |  mapped   |           |
 *    +-----------+-----------+-----------+-----------+-----------+-----------+
 *                ^           ^                                   ^
 *              um->off      rlen                          um->off + um->len
 *
 * 3) The whole mapping is outside of the safe zone:
 *
 *    0 KB        4 KB        8 KB        12 KB       16 KB       20 KB
 *    +-----------+-----------+-----------+-----------+-----------+-----------+
 *    |###########|           |           |           |           |           |
 *    |           |           |  mapped   |  mapped   |  mapped   |           |
 *    +-----------+-----------+-----------+-----------+-----------+-----------+
 *                ^           ^                                   ^
 *               rlen       um->off                            off + um->len
 *
 * Case 1) must be checked and completely ignored, as the mapping cannot be
 * affected by the truncate() call.
 *
 * Case 2) requires us to unmap 3 pages, outside of the safe zone. In order to
 * do that, we need to calculate the starting address as:
 *
 *    um->vaddr + (rlen - um->off)
 *                \_____________/
 *                     voff
 *
 * After that, we just have to calculate `vend` as:
 *
 *    um->vaddr + um->len
 *
 * No matter where we started, the ending address of the mapping will be the
 * same and it will always be > `rlen`, because we're not in case 1).
 *
 * Case 3) is the same as case 2) with the exception that `voff` is just 0.
 */

static void ramfs_unmap_past_eof_mappings(ramfs_inode *i, size_t len)
{
   const size_t rlen = round_up_at(len, PAGE_SIZE);
   struct user_mapping *um;
   uptr va;
   ASSERT(!is_preemption_enabled());

   list_for_each_ro(um, &i->mappings_list, inode_node) {

      if (um->off + um->len <= rlen)
         continue;

      const uptr voff = rlen >= um->off ? rlen - um->off : 0;
      const uptr vend = um->vaddr + um->len;

      for (va = um->vaddr + voff; va < vend; va += PAGE_SIZE) {
         unmap_page_permissive(um->pi->pdir, (void *)va, false);
         invalidate_page(va);
      }
   }
}

static int ramfs_inode_truncate(ramfs_inode *i, offt len)
{
   ASSERT(rwlock_wp_holding_exlock(&i->rwlock));

   if (len < 0 || len >= i->fsize)
      return -EINVAL;

   if (i->type == VFS_DIR)
      return -EISDIR;

   /*
    * Truncate syscalls cannot get here unless the inode is a file or a
    * directory. In case of a symlink, we'll get here the corresponding file.
    * In case of special files instead, the VFS layer should have already
    * redirected the truncate call to a different layer.
    */
   ASSERT(i->type == VFS_FILE);

   disable_preemption();
   {
      ramfs_unmap_past_eof_mappings(i, (size_t) len);
   }
   enable_preemption();

   while (true) {

      ramfs_block *b =
         bintree_get_last_obj(i->blocks_tree_root, ramfs_block, node);

      if (!b || b->offset < len)
         break;

      /* Remove the block object from the tree */
      bintree_remove_ptr(&i->blocks_tree_root,
                         b,
                         ramfs_block,
                         node,
                         offset);

      /* Free the memory pointed by this block */
      kfree2(b->vaddr, PAGE_SIZE);

      /* Free the memory used by the block object itself */
      kfree2(b, sizeof(ramfs_block));
   }

   i->fsize = len;
   i->blocks_count = round_up_at((uptr) len, PAGE_SIZE);
   return 0;
}

static int ramfs_inode_truncate_safe(ramfs_inode *i, offt len)
{
   int rc;
   rwlock_wp_exlock(&i->rwlock);
   {
      if (i->mode & 0200) { /* write permission */

         if (len < i->fsize)
            rc = ramfs_inode_truncate(i, len);
         else if (len > i->fsize)
            rc = ramfs_inode_extend(i, len);
         else
            rc = 0; /* len == i->fsize */

      } else {
         rc = -EACCES;
      }
   }
   rwlock_wp_exunlock(&i->rwlock);
   return rc;
}

static int ramfs_truncate(filesystem *fs, vfs_inode_ptr_t i, offt len)
{
   return ramfs_inode_truncate_safe(i, len);
}

static ssize_t ramfs_read(fs_handle h, char *buf, size_t len)
{
   ramfs_handle *rh = h;
   ramfs_inode *inode = rh->inode;
   offt tot_read = 0;
   offt buf_rem = (offt) len;
   ASSERT(inode->type == VFS_FILE);

   if (inode->type == VFS_DIR)
      return -EISDIR;

   while (buf_rem > 0) {

      ramfs_block *block;
      const offt page     = rh->pos & (offt)PAGE_MASK;
      const offt page_off = rh->pos & (offt)OFFSET_IN_PAGE_MASK;
      const offt page_rem = (offt)PAGE_SIZE - page_off;
      const offt file_rem = inode->fsize - rh->pos;
      const offt to_read  = MIN3(page_rem, buf_rem, file_rem);

      if (rh->pos >= inode->fsize)
         break;

      ASSERT(to_read >= 0);

      if (!to_read)
         break;

      block = bintree_find_ptr(inode->blocks_tree_root,
                               page,
                               ramfs_block,
                               node,
                               offset);

      if (block) {
         /* reading a regular block */
         memcpy(buf + tot_read, block->vaddr + page_off, (size_t)to_read);
      } else {
         /* reading a hole */
         memset(buf + tot_read, 0, (size_t)to_read);
      }

      tot_read += to_read;
      rh->pos  += to_read;
      buf_rem  -= to_read;
   }

   return (ssize_t) tot_read;
}

static ssize_t ramfs_write(fs_handle h, char *buf, size_t len)
{
   ramfs_handle *rh = h;
   ramfs_inode *inode = rh->inode;
   offt tot_written = 0;
   offt buf_rem = (offt)len;

   /* We can be sure it's a file because dirs cannot be open for writing */
   ASSERT(inode->type == VFS_FILE);

   if (rh->fl_flags & O_APPEND)
      rh->pos = inode->fsize;

   while (buf_rem > 0) {

      ramfs_block *block;
      const offt page     = rh->pos & (offt)PAGE_MASK;
      const offt page_off = rh->pos & (offt)OFFSET_IN_PAGE_MASK;
      const offt page_rem = (offt)PAGE_SIZE - page_off;
      const offt to_write = MIN(page_rem, buf_rem);

      ASSERT(to_write > 0);

      block = bintree_find_ptr(inode->blocks_tree_root,
                               page,
                               ramfs_block,
                               node,
                               offset);

      /* Assert that if page_off > 0, the block is present */
      ASSERT(!page_off || block);

      if (!block) {

         if (!(block = ramfs_new_block(page)))
            break;

         ramfs_append_new_block(inode, block);
      }

      memcpy(block->vaddr + page_off, buf + tot_written, (size_t)to_write);
      tot_written += to_write;
      buf_rem     -= to_write;
      rh->pos     += to_write;

      if (rh->pos > inode->fsize)
         inode->fsize = rh->pos;
   }

   if (len > 0 && !tot_written)
      return -ENOSPC;

   return (ssize_t)tot_written;
}
