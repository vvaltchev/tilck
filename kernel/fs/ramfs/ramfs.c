/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/process.h>      /* needed by mmap.c.h */
#include <sys/mman.h>      // system header

#include "ramfs_int.h"
#include "getdents.c.h"
#include "locking.c.h"
#include "dir_entries.c.h"
#include "inodes.c.h"
#include "stat.c.h"
#include "rw_ops.c.h"
#include "mmap.c.h"
#include "open.c.h"
#include "mkdir.c.h"

static int ramfs_unlink(vfs_path *p)
{
   ramfs_path *rp = (ramfs_path *) &p->fs_path;
   ramfs_data *d = p->fs->device_data;
   ramfs_inode *i = rp->inode;
   ramfs_inode *idir = rp->dir_inode;

   ASSERT(rwlock_wp_holding_exlock(&d->rwlock));

   if (i->type == VFS_DIR)
      return -EISDIR;

   if (!(idir->mode & 0200)) /* write permission */
      return -EACCES;

   /*
    * The only case when `rp->e` is NULL is when path == "/", but we have just
    * checked the directory case. Therefore, `rp->e` must be valid.
    */
   ASSERT(rp->dir_entry != NULL);

   /* Remove the dir entry */
   ramfs_dir_remove_entry(idir, rp->dir_entry);

   /* Trucate and delete the inode, if it's not used */
   if (!i->nlink && !get_ref_count(i)) {

      if (i->type == VFS_FILE)
         ramfs_inode_truncate_safe(i, 0);

      ramfs_destroy_inode(d, i);
   }

   return 0;
}

static int ramfs_dup(fs_handle h, fs_handle *dup_h)
{
   ramfs_handle *new_h = kmalloc(sizeof(ramfs_handle));

   if (!new_h)
      return -ENOMEM;

   memcpy(new_h, h, sizeof(ramfs_handle));
   retain_obj(new_h->inode);
   *dup_h = new_h;
   return 0;
}

static void ramfs_close(fs_handle h)
{
   ramfs_handle *rh = h;
   ramfs_inode *i = rh->inode;

   if (i->type == VFS_DIR) {
      /* Remove this handle from h->inode->handles_list */
      list_remove(&rh->node);
   }

   release_obj(i);

   if (!get_ref_count(i) && !i->nlink) {

      /*
       * !get_ref_count(i) => no handle referring to this inode
       * !i->nlink         => no dir entry referring to this inode
       *
       * It means the last link (dir entry) pointing to this inode has been
       * removed while the current task was keeping opened a handle to this
       * inode. Now, nobody can get to this inode anymore. We have to destroy
       * it.
       */

      if (i->type == VFS_FILE)
         ramfs_inode_truncate_safe(i, 0);

      ramfs_destroy_inode(rh->fs->device_data, i);
   }

   kfree2(rh, sizeof(ramfs_handle));
}

/*
 * This function is supposed to be called ONLY by ramfs_create() in its error
 * path, as a clean-up. It is *not* a proper way to destroy a whole ramfs
 * instance after unmounting it.
 */
static void ramfs_err_case_destroy(filesystem *fs)
{
   ramfs_data *d = fs->device_data;

   if (d) {

      if (d->root) {
         ramfs_destroy_inode(d, d->root);
      }

      rwlock_wp_destroy(&d->rwlock);
      kfree2(d, sizeof(ramfs_data));
   }

   kfree2(fs, sizeof(filesystem));
}

static void
ramfs_get_entry(filesystem *fs,
                void *dir_inode,
                const char *name,
                ssize_t name_len,
                fs_path_struct *fs_path)
{
   ramfs_data *d = fs->device_data;
   ramfs_inode *idir = dir_inode;
   ramfs_entry *re;

   if (!dir_inode) {

      *fs_path = (fs_path_struct) {
         .inode = d->root,
         .dir_inode = d->root,
         .dir_entry = NULL,
         .type = VFS_DIR,
      };

      return;
   }

   re = ramfs_dir_get_entry_by_name(idir, name, name_len);

   *fs_path = (fs_path_struct) {
      .inode      = re ? re->inode : NULL,
      .dir_inode  = idir,
      .dir_entry  = re,
      .type       = re ? re->inode->type : VFS_NONE,
   };
}

static vfs_inode_ptr_t ramfs_getinode(fs_handle h)
{
   return ((ramfs_handle *)h)->inode;
}

static int ramfs_symlink(const char *target, vfs_path *lp)
{
   ramfs_data *d = lp->fs->device_data;
   ramfs_inode *n;

   n = ramfs_create_inode_symlink(d, lp->fs_path.dir_inode, target);

   if (!n)
      return -ENOSPC;

   return ramfs_dir_add_entry(lp->fs_path.dir_inode, lp->last_comp, n);
}

/* NOTE: `buf` is guaranteed to have room for at least MAX_PATH chars */
static int ramfs_readlink(vfs_path *p, char *buf)
{
   ramfs_inode *i = p->fs_path.inode;

   if (i->type != VFS_SYMLINK)
      return -EINVAL;

   memcpy(buf, i->path, i->path_len); /* skip final \0 */
   return (int) i->path_len;
}

static int ramfs_retain_inode(filesystem *fs, vfs_inode_ptr_t inode)
{
   ASSERT(inode != NULL);

   if (!(fs->flags & VFS_FS_RW))
      return 1;

   return retain_obj((ramfs_inode *)inode);
}

static int ramfs_release_inode(filesystem *fs, vfs_inode_ptr_t inode)
{
   ASSERT(inode != NULL);

   if (!(fs->flags & VFS_FS_RW))
      return 1;

   return release_obj((ramfs_inode *)inode);
}

static int ramfs_chmod(filesystem *fs, vfs_inode_ptr_t inode, mode_t mode)
{
   ramfs_inode *i = inode;
   int rc;

   rwlock_wp_exlock(&i->rwlock);
   {
      const mode_t special_bits = mode & (mode_t)~0777;
      const mode_t curr_spec_bits = i->mode & (mode_t)~0777;

      if (!special_bits || special_bits == curr_spec_bits) {

         i->mode = curr_spec_bits | (mode & 0777);
         rc = 0;

      } else {

         /* Special bits (e.g. sticky bit etc.) are not supported by Tilck */
         rc = -EPERM;
      }
   }
   rwlock_wp_exunlock(&i->rwlock);
   return rc;
}

static int ramfs_rename(filesystem *fs, vfs_path *voldp, vfs_path *vnewp)
{
   ramfs_path *oldp = (void *)&voldp->fs_path;
   ramfs_path *newp = (void *)&vnewp->fs_path;
   int rc;

   DEBUG_ONLY_UNSAFE(ramfs_data *d = fs->device_data);
   ASSERT(rwlock_wp_holding_exlock(&d->rwlock));

   if (newp->inode != NULL) {

      if (newp->type == VFS_DIR) {

         if (oldp->type != VFS_DIR)
            return -EISDIR;

         if (newp->inode->num_entries > 2)
            return -ENOTEMPTY;

         if ((rc = ramfs_rmdir(vnewp)))
            return rc;

      } else {

         if ((rc = ramfs_unlink(vnewp)))
            return rc;
      }
   }

   rc = ramfs_dir_add_entry(newp->dir_inode, vnewp->last_comp, oldp->inode);

   if (rc) {

      /*
       * Note: the only way this last call could fail is OOM case because of a
       * race condition exactly between the rmdir/unlink operation and the
       * creation of the new entry. In that case, the rename syscall will fail
       * and the destination path (if existing) would be deleted. This is a rare
       * case of failing syscall having a side-effect. The eventual problem
       * could be fixed by disabling the preemption here or by avoiding the
       * destruction of the entry object and then resuing it. Both of these
       * solutions aren't very elegant and it seems like it's not worth to
       * implement either of them, at least at the moment; in the future the
       * trade-off might change.
       */
      return rc;
   }

   /* Finally, this operation cannot fail. */
   ramfs_dir_remove_entry(oldp->dir_inode, oldp->dir_entry);
   return 0;
}

static int ramfs_link(filesystem *fs, vfs_path *voldp, vfs_path *vnewp)
{
   ramfs_path *oldp = (void *)&voldp->fs_path;
   ramfs_path *newp = (void *)&vnewp->fs_path;

   if (oldp->type != VFS_FILE)
      return -EPERM;

   if (newp->inode != NULL)
      return -EEXIST;

   return ramfs_dir_add_entry(newp->dir_inode, vnewp->last_comp, oldp->inode);
}

static const fs_ops static_fsops_ramfs =
{
   .get_inode = ramfs_getinode,
   .open = ramfs_open,
   .close = ramfs_close,
   .dup = ramfs_dup,
   .getdents = ramfs_getdents,
   .unlink = ramfs_unlink,
   .mkdir = ramfs_mkdir,
   .rmdir = ramfs_rmdir,
   .truncate = ramfs_truncate,
   .stat = ramfs_stat,
   .symlink = ramfs_symlink,
   .readlink = ramfs_readlink,
   .chmod = ramfs_chmod,
   .get_entry = ramfs_get_entry,
   .rename = ramfs_rename,
   .link = ramfs_link,
   .retain_inode = ramfs_retain_inode,
   .release_inode = ramfs_release_inode,

   .fs_exlock = ramfs_exlock,
   .fs_exunlock = ramfs_exunlock,
   .fs_shlock = ramfs_shlock,
   .fs_shunlock = ramfs_shunlock,
};

filesystem *ramfs_create(void)
{
   filesystem *fs;
   ramfs_data *d;

   if (!(fs = kzmalloc(sizeof(filesystem))))
      return NULL;

   if (!(d = kzmalloc(sizeof(ramfs_data)))) {
      ramfs_err_case_destroy(fs);
      return NULL;
   }

   fs->device_data = d;
   rwlock_wp_init(&d->rwlock, false);
   d->next_inode_num = 1;
   d->root = ramfs_create_inode_dir(d, 0777, NULL);

   if (!d->root) {
      ramfs_err_case_destroy(fs);
      return NULL;
   }

   fs->fs_type_name = "ramfs";
   fs->device_id = vfs_get_new_device_id();
   fs->flags = VFS_FS_RW;
   fs->fsops = &static_fsops_ramfs;
   return fs;
}

