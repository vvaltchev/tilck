/* SPDX-License-Identifier: BSD-2-Clause */

static int ramfs_file_ioctl(fs_handle h, uptr cmd, void *argp)
{
   return -EINVAL;
}

static int ramfs_file_fcntl(fs_handle h, int cmd, int arg)
{
   return -EINVAL;
}

static off_t ramfs_file_seek(fs_handle h, off_t off, int whence)
{
   NOT_IMPLEMENTED();
}

static sptr ramfs_insert_remove_block_cmp(const void *a, const void *b)
{
   const ramfs_block *b1 = a;
   const ramfs_block *b2 = b;
   return (sptr)b1->offset - (sptr)b2->offset;
}

static sptr ramfs_find_block_cmp(const void *obj, const void *valptr)
{
   const ramfs_block *block = obj;
   size_t searched_off = *(const size_t *)valptr;
   return (sptr)block->offset - (sptr)searched_off;
}

static ssize_t ramfs_file_read(fs_handle h, char *buf, size_t len)
{
   ramfs_handle *rh = h;
   ramfs_inode *inode = rh->inode;
   size_t tot_read = 0;
   size_t buf_rem = len;
   ASSERT(inode->type == RAMFS_FILE);

   while (buf_rem > 0) {

      ramfs_block *block;
      size_t page = rh->read_pos & PAGE_MASK;
      size_t page_off = rh->read_pos & OFFSET_IN_PAGE_MASK;
      size_t page_rem = PAGE_SIZE - page_off;
      size_t file_rem = inode->fsize - rh->read_pos;
      size_t to_read = MIN3(page_rem, buf_rem, file_rem);

      ASSERT(to_read > 0 || !file_rem);

      if (!to_read)
         break;

      block = bintree_find(inode->blocks_tree_root,
                           &page,
                           ramfs_find_block_cmp,
                           ramfs_block,
                           node);

      if (block) {
         /* reading a regular block */
         memcpy(buf + tot_read, block->vaddr + page_off, to_read);
      } else {
         /* reading a hole */
         memset(buf + tot_read, 0, to_read);
      }

      tot_read += to_read;
      rh->read_pos += to_read;
      buf_rem -= to_read;
   }

   return (ssize_t) tot_read;
}

static ssize_t ramfs_file_write(fs_handle h, char *buf, size_t len)
{
   ramfs_handle *rh = h;
   ramfs_inode *inode = rh->inode;
   size_t tot_written = 0;
   size_t buf_rem = len;
   ASSERT(inode->type == RAMFS_FILE);

   while (buf_rem > 0) {

      ramfs_block *block;
      size_t page = rh->write_pos & PAGE_MASK;
      size_t page_off = rh->write_pos & OFFSET_IN_PAGE_MASK;
      size_t page_rem = PAGE_SIZE - page_off;
      size_t to_write = MIN(page_rem, buf_rem);

      ASSERT(to_write > 0);

      block = bintree_find(inode->blocks_tree_root,
                           &page,
                           ramfs_find_block_cmp,
                           ramfs_block,
                           node);

      /* Assert that if page_off > 0, the block is present */
      ASSERT(!page_off || block);

      if (!block) {

         void *vaddr = kmalloc(PAGE_SIZE);

         if (!vaddr)
            break;

         if (!(block = kmalloc(sizeof(ramfs_block)))) {
            kfree2(vaddr, PAGE_SIZE);
            break;
         }

         bintree_node_init(&block->node);
         block->offset = page;
         block->vaddr = vaddr;

         bintree_insert(&inode->blocks_tree_root,
                        block,
                        ramfs_insert_remove_block_cmp,
                        ramfs_block,
                        node);

         /* We created a new block, therefore we must increase file's size */
         inode->fsize += to_write;
         inode->blocks_count++;
      }

      memcpy(block->vaddr + page_off, buf + tot_written, to_write);
      tot_written += to_write;
      rh->write_pos += to_write;
      buf_rem -= to_write;
   }

   if (len > 0 && !tot_written)
      return -ENOSPC;

   return (ssize_t)tot_written;
}
