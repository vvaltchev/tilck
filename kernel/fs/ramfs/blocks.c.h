/* SPDX-License-Identifier: BSD-2-Clause */

static struct ramfs_block *ramfs_new_block(offt page)
{
   struct ramfs_block *b;

   /* Allocate memory for the block object */
   if (!(b = kmalloc(sizeof(struct ramfs_block))))
      return NULL;

   /* Allocate block's data */
   if (!(b->vaddr = kzmalloc(PAGE_SIZE))) {
      kfree2(b, sizeof(struct ramfs_block));
      return NULL;
   }

   /* Retain the pageframe used by this block */
   retain_pageframes_mapped_at(get_kernel_pdir(), b->vaddr, PAGE_SIZE);

   /* Init the block object */
   bintree_node_init(&b->node);
   b->offset = page;
   return b;
}

static void ramfs_destroy_block(struct ramfs_block *b)
{
   /* Release the pageframe used by this block */
   release_pageframes_mapped_at(get_kernel_pdir(), b->vaddr, PAGE_SIZE);

   /* Free the memory pointed by this block */
   kfree2(b->vaddr, PAGE_SIZE);

   /* Free the memory used by the block object itself */
   kfree2(b, sizeof(struct ramfs_block));
}

static void
ramfs_append_new_block(struct ramfs_inode *inode, struct ramfs_block *block)
{
   DEBUG_ONLY_UNSAFE(bool success =)
      bintree_insert_ptr(&inode->blocks_tree_root,
                         block,
                         struct ramfs_block,
                         node,
                         offset);

   ASSERT(success);
   inode->blocks_count++;
}

static int ramfs_inode_extend(struct ramfs_inode *i, offt new_len)
{
   ASSERT(rwlock_wp_holding_exlock(&i->rwlock));
   ASSERT(new_len > i->fsize);

   i->fsize = new_len;
   return 0;
}
