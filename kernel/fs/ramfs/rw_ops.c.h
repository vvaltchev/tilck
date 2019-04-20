/* SPDX-License-Identifier: BSD-2-Clause */
#include <tilck/common/utils.h>

static int ramfs_ioctl(fs_handle h, uptr cmd, void *argp)
{
   return -EINVAL;
}

static int ramfs_fcntl(fs_handle h, int cmd, int arg)
{
   return -EINVAL;
}

static off_t ramfs_seek(fs_handle h, off_t off, int whence)
{
   ramfs_handle *rh = h;

   switch (whence) {

      case SEEK_SET:
         rh->pos = off;
         break;

      case SEEK_CUR:
         rh->pos += off;
         break;

      case SEEK_END:
         rh->pos = (off_t)rh->inode->fsize + off;
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

static int ramfs_drop_blocks(void *obj_block, void *arg)
{
   ramfs_block *b = obj_block;
   off_t cutoff = *(off_t *)arg;

   if ((off_t)b->offset < cutoff)
      return 0;

   kfree2(b->vaddr, PAGE_SIZE);
   kfree2(b, sizeof(ramfs_block));

   /* The bintree MUST NOT use the obj after the callback returns */
   DEBUG_ONLY(bzero(b, sizeof(ramfs_block)));
   return 0;
}

static int ramfs_inode_truncate(ramfs_inode *i, off_t len)
{
   ASSERT(rwlock_wp_holding_exlock(&i->rwlock));

   if (len < 0 || len > i->fsize)
      return -EINVAL;

   bintree_in_order_visit(i->blocks_tree_root,
                          ramfs_drop_blocks,
                          &len,
                          ramfs_block,
                          node);

   i->blocks_tree_root = NULL;
   i->fsize = len;
   i->blocks_count = round_up_at((uptr) len, PAGE_SIZE);
   return 0;
}

static ssize_t ramfs_read(fs_handle h, char *buf, size_t len)
{
   ramfs_handle *rh = h;
   ramfs_inode *inode = rh->inode;
   off_t tot_read = 0;
   off_t buf_rem = (off_t) len;
   ASSERT(inode->type == RAMFS_FILE);

   if (inode->type == RAMFS_DIRECTORY)
      return -EISDIR;

   while (buf_rem > 0) {

      ramfs_block *block;
      const off_t page     = rh->pos & (off_t)PAGE_MASK;
      const off_t page_off = rh->pos & (off_t)OFFSET_IN_PAGE_MASK;
      const off_t page_rem = (off_t)PAGE_SIZE - page_off;
      const off_t file_rem = inode->fsize - rh->pos;
      const off_t to_read  = MIN3(page_rem, buf_rem, file_rem);

      if (rh->pos >= inode->fsize)
         break;

      ASSERT(to_read >= 0);

      if (!to_read)
         break;

      block = bintree_find(inode->blocks_tree_root,
                           &page,
                           ramfs_find_block_cmp,
                           ramfs_block,
                           node);


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
   off_t tot_written = 0;
   off_t buf_rem = (off_t)len;

   /* We can be sure it's a file because dirs cannot be open for writing */
   ASSERT(inode->type == RAMFS_FILE);

   while (buf_rem > 0) {

      ramfs_block *block;
      const off_t page     = rh->pos & (off_t)PAGE_MASK;
      const off_t page_off = rh->pos & (off_t)OFFSET_IN_PAGE_MASK;
      const off_t page_rem = (off_t)PAGE_SIZE - page_off;
      const off_t to_write = MIN(page_rem, buf_rem);

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

         inode->blocks_count++;
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
