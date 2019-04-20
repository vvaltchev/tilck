/* SPDX-License-Identifier: BSD-2-Clause */
#include <tilck/common/utils.h>

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

   if (len < 0 || (size_t)len > i->fsize)
      return -EINVAL;

   bintree_in_order_visit(i->blocks_tree_root,
                          ramfs_drop_blocks,
                          &len,
                          ramfs_block,
                          node);

   i->blocks_tree_root = NULL;
   i->fsize = (size_t) len;
   i->blocks_count = round_up_at((uptr) len, PAGE_SIZE);
   return 0;
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
      size_t page = (size_t)rh->pos & PAGE_MASK;
      size_t page_off = (size_t)rh->pos & OFFSET_IN_PAGE_MASK;
      size_t page_rem = PAGE_SIZE - page_off;

      if (rh->pos >= (off_t)inode->fsize)
         break;

      size_t file_rem = (size_t)inode->fsize - (size_t)rh->pos;
      size_t to_read = MIN3(page_rem, buf_rem, file_rem);

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
      rh->pos += to_read;
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
      size_t page = (size_t)rh->pos & PAGE_MASK;
      size_t page_off = (size_t)rh->pos & OFFSET_IN_PAGE_MASK;
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

         inode->blocks_count++;
      }

      memcpy(block->vaddr + page_off, buf + tot_written, to_write);
      tot_written += to_write;
      buf_rem -= to_write;
      rh->pos += to_write;

      if ((size_t)rh->pos > inode->fsize)
         inode->fsize = (size_t) rh->pos;
   }

   if (len > 0 && !tot_written)
      return -ENOSPC;

   return (ssize_t)tot_written;
}
