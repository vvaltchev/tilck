/* SPDX-License-Identifier: BSD-2-Clause */

static int ramfs_munmap(fs_handle h, void *vaddrp, size_t len)
{
   return generic_fs_munmap(h, vaddrp, len);
}

static int ramfs_mmap(struct user_mapping *um, bool register_only)
{
   struct process *pi = get_curr_proc();
   struct ramfs_handle *rh = um->h;
   struct ramfs_inode *i = rh->inode;
   ulong vaddr = um->vaddr;
   struct bintree_walk_ctx ctx;
   struct ramfs_block *b;
   int rc;

   const size_t off_begin = um->off;
   const size_t off_end = off_begin + um->len;

   ASSERT(IS_PAGE_ALIGNED(um->len));

   if (i->type != VFS_FILE)
      return -EACCES;

   if (register_only)
      goto register_mapping;

   bintree_in_order_visit_start(&ctx,
                                i->blocks_tree_root,
                                struct ramfs_block,
                                node,
                                false);

   while ((b = bintree_in_order_visit_next(&ctx))) {

      if ((size_t)b->offset < off_begin)
         continue; /* skip this block */

      if ((size_t)b->offset >= off_end)
         break;

      rc = map_page(pi->pdir,
                    (void *)vaddr,
                    KERNEL_VA_TO_PA(b->vaddr),
                    true,
                    rh->fl_flags & O_RDWR);

      if (rc) {

         /* mmap failed, we have to unmap the pages already mappped */
         vaddr -= PAGE_SIZE;

         for (; vaddr >= um->vaddr; vaddr -= PAGE_SIZE) {
            unmap_page_permissive(pi->pdir, (void *)vaddr, false);
         }

         return rc;
      }

      vaddr += PAGE_SIZE;
   }

register_mapping:
   list_add_tail(&i->mappings_list, &um->inode_node);
   return 0;
}

static bool
ramfs_handle_fault_int(struct process *pi,
                       struct ramfs_handle *rh,
                       void *vaddrp,
                       bool p,
                       bool rw)
{
   ulong vaddr = (ulong) vaddrp;
   ulong abs_off;
   struct ramfs_block *block;
   int rc;
   struct user_mapping *um = process_get_user_mapping(vaddrp);

   if (!um)
      return false; /* Weird, but it's OK */

   ASSERT(um->h == rh);

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
                 KERNEL_VA_TO_PA(rw ? block->vaddr : &zero_page),
                 true,
                 true);

   if (rc)
      panic("Out-of-memory: unable to map a ramfs_block. No OOM killer");

   invalidate_page(vaddr);
   return true;
}


static bool
ramfs_handle_fault(fs_handle h, void *vaddrp, bool p, bool rw)
{
   bool ret;
   struct process *pi = get_curr_proc();

   disable_preemption();
   {
      ret = ramfs_handle_fault_int(pi, h, vaddrp, p, rw);
   }
   enable_preemption();
   return ret;
}
