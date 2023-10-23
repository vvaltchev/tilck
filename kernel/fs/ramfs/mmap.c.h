/* SPDX-License-Identifier: BSD-2-Clause */

static int ramfs_munmap(struct user_mapping *um, void *vaddrp, size_t len)
{
   return generic_fs_munmap(um, vaddrp, len);
}

static int
ramfs_mmap(struct user_mapping *um, pdir_t *pdir, int flags)
{
   struct ramfs_handle *rh = um->h;
   struct ramfs_inode *i = rh->inode;
   ulong vaddr = um->vaddr;
   struct bintree_walk_ctx ctx;
   struct ramfs_block *b;
   u32 pg_flags;
   int rc;

   const size_t off_begin = um->off;
   const size_t off_end = off_begin + um->len;

   ASSERT(IS_PAGE_ALIGNED(um->len));

   if (i->type != VFS_FILE)
      return -EACCES;

   if (flags & VFS_MM_DONT_MMAP)
      goto register_mapping;

   bintree_in_order_visit_start(&ctx,
                                i->blocks_tree_root,
                                struct ramfs_block,
                                node,
                                false);

   pg_flags = PAGING_FL_US | PAGING_FL_SHARED;

   if ((rh->fl_flags & O_RDWR) == O_RDWR)
      pg_flags |= PAGING_FL_RW;

   while ((b = bintree_in_order_visit_next(&ctx))) {

      if ((size_t)b->offset < off_begin)
         continue; /* skip this block */

      if ((size_t)b->offset >= off_end)
         break;

      rc = map_page(pdir,
                    (void *)vaddr,
                    LIN_VA_TO_PA(b->vaddr),
                    pg_flags);

      if (rc) {

         /* mmap failed, we have to unmap the pages already mapped */
         vaddr -= PAGE_SIZE;

         for (; vaddr >= um->vaddr; vaddr -= PAGE_SIZE) {
            unmap_page_permissive(pdir, (void *)vaddr, false);
         }

         return rc;
      }

      vaddr += PAGE_SIZE;
   }

register_mapping:
   if (!(flags & VFS_MM_DONT_REGISTER)) {
      list_add_tail(&i->mappings_list, &um->inode_node);
   }

   return 0;
}

static bool
ramfs_handle_fault_int(struct process *pi,
                       struct user_mapping *um,
                       void *vaddrp,
                       bool p,
                       bool rw)
{
   struct ramfs_handle *rh = um->h;
   ulong vaddr = (ulong) vaddrp;
   ulong abs_off;
   struct ramfs_block *block;
   int rc;

   ASSERT(um != NULL);

   if (p) {

      /*
       * The page is present, just is read-only and the user code tried to
       * write: there's nothing we can do.
       */

      ASSERT(rw);
      ASSERT((um->prot & PROT_WRITE) == 0);
      return false;
   }

   /* The page is *not* present */
   abs_off = um->off + (vaddr - um->vaddr);

   if (abs_off >= (ulong)rh->inode->fsize)
      return false; /* Read/write past EOF */

   if (rw) {
      /* Create and map on-the-fly a struct ramfs_block */
      if (!(block = ramfs_new_block((offt)(abs_off & PAGE_MASK))))
         panic("Out-of-memory: unable to alloc a ramfs_block. No OOM killer");

      ramfs_append_new_block(rh->inode, block);
   }

   rc = map_page(pi->pdir,
                 (void *)(vaddr & PAGE_MASK),
                 rw ? LIN_VA_TO_PA(block->vaddr) : KERNEL_VA_TO_PA(&zero_page),
                 PAGING_FL_US | PAGING_FL_RW | PAGING_FL_SHARED);

   if (rc)
      panic("Out-of-memory: unable to map a ramfs_block. No OOM killer");

   invalidate_page(vaddr);
   return true;
}


static bool
ramfs_handle_fault(struct user_mapping *um, void *vaddrp, bool p, bool rw)
{
   bool ret;
   struct process *pi = get_curr_proc();

   disable_preemption();
   {
      ret = ramfs_handle_fault_int(pi, um, vaddrp, p, rw);
   }
   enable_preemption();
   return ret;
}
