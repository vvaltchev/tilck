/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/process.h>

static int ramfs_munmap(fs_handle h, void *vaddrp, size_t len)
{
   process_info *pi = get_curr_task()->pi;
   //ramfs_handle *rh = h;
   //ramfs_inode *i = rh->inode;
   uptr vaddr = (uptr)vaddrp;
   uptr vend = vaddr + len;
   ASSERT(len % PAGE_SIZE == 0);

   //printk("[ramfs] UNMAP inode %llu at [%p, %p)\n", i->ino, vaddr, vaddr+len);

   for (; vaddr < vend; vaddr += PAGE_SIZE) {
      unmap_page_permissive(pi->pdir, (void *)vaddr, false);
   }

   return 0;
}

static int ramfs_mmap(fs_handle h, void *vaddrp, size_t len, int prot)
{
   process_info *pi = get_curr_task()->pi;
   ramfs_handle *rh = h;
   ramfs_inode *i = rh->inode;
   uptr vaddr = (uptr) vaddrp;
   bintree_walk_ctx ctx;
   ramfs_block *b;
   int rc;

   ASSERT(len % PAGE_SIZE == 0);

   if (i->type != VFS_FILE)
      return -EACCES;

   //printk("[ramfs] mmap inode %llu at [%p, %p)\n", i->ino, vaddr, vaddr+len);

   bintree_in_order_visit_start(&ctx,
                                i->blocks_tree_root,
                                ramfs_block,
                                node,
                                false);

   while ((b = bintree_in_order_visit_next(&ctx))) {

      if ((size_t)b->offset >= len)
         break;

      //printk("[ramfs] block at offset %u -> %p\n", b->offset, b->vaddr);

      rc = map_page(pi->pdir,
                    (void *)vaddr,
                    KERNEL_VA_TO_PA(b->vaddr),
                    true,
                    rh->fl_flags & O_RDWR);

      if (rc) {

         /* mmap failed, we have to unmap the pages already mappped */
         vaddr -= PAGE_SIZE;

         for (; vaddr >= (uptr)vaddrp; vaddr -= PAGE_SIZE) {
            unmap_page_permissive(pi->pdir, (void *)vaddr, false);
         }

         return rc;
      }

      vaddr += PAGE_SIZE;
   }

   return 0;
}


static const file_ops static_ops_ramfs =
{
   .read = ramfs_read,
   .write = ramfs_write,
   .seek = ramfs_seek,
   .ioctl = ramfs_ioctl,
   .fcntl = ramfs_fcntl,
   .mmap = ramfs_mmap,
   .munmap = ramfs_munmap,
   .exlock = ramfs_file_exlock,
   .exunlock = ramfs_file_exunlock,
   .shlock = ramfs_file_shlock,
   .shunlock = ramfs_file_shunlock,
};

static int
ramfs_open_int(filesystem *fs, ramfs_inode *inode, fs_handle *out, int fl)
{
   ramfs_handle *h;

   if (!(h = kzmalloc(sizeof(ramfs_handle))))
      return -ENOMEM;

   h->inode = inode;
   h->fs = fs;
   h->fops = &static_ops_ramfs;
   retain_obj(inode);

   if (inode->type == VFS_DIR) {

      /*
       * If we're opening a directory, register its handle in inode's handles
       * list so that if unlink() is called on an entry E and there are open
       * handles to E's parent-dir where h->dpos == E, their dpos is moved
       * forward. This is a VERY CORNER CASE, but it *MUST BE* handled.
       */
      list_node_init(&h->node);
      list_add_tail(&inode->handles_list, &h->node);
      h->dpos = list_first_obj(&inode->entries_list, ramfs_entry, lnode);
   }

   if (fl & O_TRUNC) {
      ramfs_inode_truncate_safe(inode, 0);
   }

   *out = h;
   return 0;
}

static int ramfs_open_existing_checks(int fl, ramfs_inode *i)
{
   if (!(fl & O_WRONLY) && !(i->mode & 0400))
      return -EACCES;

   if ((fl & O_WRONLY) && !(i->mode & 0200))
      return -EACCES;

   if ((fl & O_RDWR) && !(i->mode & 0600))
      return -EACCES;

   if ((fl & O_DIRECTORY) && (i->type != VFS_DIR))
      return -ENOTDIR;

   if ((fl & O_CREAT) && (fl & O_EXCL))
      return -EEXIST;

   if ((fl & (O_WRONLY | O_RDWR)) && i->type == VFS_DIR)
      return -EISDIR;

   /*
    * On some systems O_TRUNC | O_RDONLY has undefined behavior and on some
    * the file might actually be truncated. On Tilck, that is simply NOT
    * allowed.
    */
   if ((fl & O_TRUNC) && !(fl & (O_WRONLY | O_RDWR)))
      return -EINVAL;

   return 0;
}

static int
ramfs_open(vfs_path *p, fs_handle *out, int fl, mode_t mod)
{
   ramfs_path *rp = (ramfs_path *) &p->fs_path;
   ramfs_data *d = p->fs->device_data;
   ramfs_inode *i = rp->inode;
   ramfs_inode *idir = rp->dir_inode;
   int rc;

   if (!i) {

      if (!(fl & O_CREAT))
         return -ENOENT;

      if (!(idir->mode & 0300)) /* write + execute */
         return -EACCES;

      if (!(i = ramfs_create_inode_file(d, mod, idir)))
         return -ENOSPC;

      if ((rc = ramfs_dir_add_entry(idir, p->last_comp, i))) {
         ramfs_destroy_inode(d, i);
         return rc;
      }

   } else {

      if (!(idir->mode & 0500)) /* read + execute */
         return -EACCES;

      if ((rc = ramfs_open_existing_checks(fl, i)))
         return rc;
   }

   return ramfs_open_int(p->fs, i, out, fl);
}
