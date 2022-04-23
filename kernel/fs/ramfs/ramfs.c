/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/process.h>
#include <tilck/kernel/fs/flock.h>

#include <sys/mman.h>      // system header

#include "ramfs_int.h"
#include "getdents.c.h"
#include "locking.c.h"
#include "dir_entries.c.h"
#include "inodes.c.h"
#include "stat.c.h"
#include "blocks.c.h"
#include "mmap.c.h"
#include "rw_ops.c.h"
#include "open.c.h"
#include "mkdir.c.h"

static int ramfs_unlink(struct vfs_path *p)
{
   struct ramfs_path *rp = (struct ramfs_path *) &p->fs_path;
   struct ramfs_data *d = p->fs->device_data;
   struct ramfs_inode *i = rp->inode;
   struct ramfs_inode *idir = rp->dir_inode;

   ASSERT(rwlock_wp_holding_exlock(&d->rwlock));

   if (i->type == VFS_DIR)
      return -EISDIR;

   if ((idir->mode & 0200) != 0200) /* write permission */
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

      if (i->type == VFS_FILE) {
         DEBUG_ONLY_UNSAFE(int rc =)
            ramfs_inode_truncate_safe(i, 0, true /* no_perm_check */);

         ASSERT(rc == 0);
      }

      ramfs_destroy_inode(d, i);
   }

   return 0;
}

static void ramfs_on_close(fs_handle h)
{
   struct ramfs_handle *rh = h;
   struct ramfs_inode *i = rh->inode;

   if (i->type == VFS_DIR) {
      /* Remove this handle from h->inode->handles_list */
      list_remove(&rh->node);
   }
}

static void ramfs_on_close_last_handle(fs_handle h)
{
   struct ramfs_handle *rh = h;
   struct ramfs_inode *i = rh->inode;

   if (!i->nlink) {

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
         ramfs_inode_truncate_safe(i, 0, true);

      ramfs_destroy_inode(rh->fs->device_data, i);
   }
}


/*
 * This function is supposed to be called ONLY by ramfs_create() in its error
 * path, as a clean-up. It is *not* a proper way to destroy a whole ramfs
 * instance after unmounting it.
 */
static void ramfs_err_case_destroy(struct mnt_fs *fs)
{
   struct ramfs_data *d = fs->device_data;

   if (d) {

      if (d->root) {
         ramfs_destroy_inode(d, d->root);
      }

      rwlock_wp_destroy(&d->rwlock);
      kfree_obj(d, struct ramfs_data);
   }

   destory_fs_obj(fs);
}

static void
ramfs_get_entry(struct mnt_fs *fs,
                void *dir_inode,
                const char *name,
                ssize_t name_len,
                struct fs_path *fs_path)
{
   struct ramfs_data *d = fs->device_data;
   struct ramfs_inode *idir = dir_inode;
   struct ramfs_entry *re;

   if (!dir_inode) {

      *fs_path = (struct fs_path) {
         .inode = d->root,
         .dir_inode = d->root,
         .dir_entry = NULL,
         .type = VFS_DIR,
      };

      return;
   }

   re = ramfs_dir_get_entry_by_name(idir, name, name_len);

   *fs_path = (struct fs_path) {
      .inode      = re ? re->inode : NULL,
      .dir_inode  = idir,
      .dir_entry  = re,
      .type       = re ? re->inode->type : VFS_NONE,
   };
}

static vfs_inode_ptr_t ramfs_getinode(fs_handle h)
{
   return ((struct ramfs_handle *)h)->inode;
}

static int ramfs_symlink(const char *target, struct vfs_path *lp)
{
   struct ramfs_data *d = lp->fs->device_data;
   struct ramfs_inode *n;

   n = ramfs_create_inode_symlink(d, lp->fs_path.dir_inode, target);

   if (!n)
      return -ENOSPC;

   return ramfs_dir_add_entry(lp->fs_path.dir_inode, lp->last_comp, n);
}

/* NOTE: `buf` is guaranteed to have room for at least MAX_PATH chars */
static int ramfs_readlink(struct vfs_path *p, char *buf)
{
   struct ramfs_inode *i = p->fs_path.inode;

   if (i->type != VFS_SYMLINK)
      return -EINVAL;

   memcpy(buf, i->path, i->path_len); /* skip final \0 */
   return (int) i->path_len;
}

static int ramfs_retain_inode(struct mnt_fs *fs, vfs_inode_ptr_t inode)
{
   ASSERT(inode != NULL);

   if (!(fs->flags & VFS_FS_RW))
      return 1;

   return retain_obj((struct ramfs_inode *)inode);
}

static int ramfs_release_inode(struct mnt_fs *fs, vfs_inode_ptr_t inode)
{
   ASSERT(inode != NULL);

   if (!(fs->flags & VFS_FS_RW))
      return 1;

   return release_obj((struct ramfs_inode *)inode);
}

static int ramfs_chmod(struct mnt_fs *fs, vfs_inode_ptr_t inode, mode_t mode)
{
   struct ramfs_inode *i = inode;
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

static int
ramfs_rename(struct mnt_fs *fs, struct vfs_path *voldp, struct vfs_path *vnewp)
{
   struct ramfs_path *oldp = (void *)&voldp->fs_path;
   struct ramfs_path *newp = (void *)&vnewp->fs_path;
   int rc;

   DEBUG_ONLY_UNSAFE(struct ramfs_data *d = fs->device_data);
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

static int
ramfs_link(struct mnt_fs *fs, struct vfs_path *voldp, struct vfs_path *vnewp)
{
   struct ramfs_path *oldp = (void *)&voldp->fs_path;
   struct ramfs_path *newp = (void *)&vnewp->fs_path;

   if (oldp->type != VFS_FILE)
      return -EPERM;

   if (newp->inode != NULL)
      return -EEXIST;

   return ramfs_dir_add_entry(newp->dir_inode, vnewp->last_comp, oldp->inode);
}

int ramfs_futimens(struct mnt_fs *fs,
                   vfs_inode_ptr_t inode,
                   const struct k_timespec64 times[2])
{
   struct ramfs_inode *i = inode;

   if ((i->mode & 0200) != 0200)
      return -EACCES;

   i->mtime = times[1];
   return 0;
}

static const struct fs_ops static_fsops_ramfs =
{
   .get_inode = ramfs_getinode,
   .open = ramfs_open,
   .on_close = ramfs_on_close,
   .on_close_last_handle = ramfs_on_close_last_handle,
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
   .futimens = ramfs_futimens,
   .retain_inode = ramfs_retain_inode,
   .release_inode = ramfs_release_inode,

   .fs_exlock = ramfs_exlock,
   .fs_exunlock = ramfs_exunlock,
   .fs_shlock = ramfs_shlock,
   .fs_shunlock = ramfs_shunlock,
};

struct mnt_fs *ramfs_create(void)
{
   struct mnt_fs *fs;
   struct ramfs_data *d;

   if (!(d = kzalloc_obj(struct ramfs_data)))
      return NULL;

   fs = create_fs_obj("ramfs", &static_fsops_ramfs, d, VFS_FS_RW);

   if (!fs) {
      kfree_obj(d, struct ramfs_data);
      return NULL;
   }

   rwlock_wp_init(&d->rwlock, false);
   d->next_inode_num = 1;
   d->root = ramfs_create_inode_dir(d, 0777, NULL);

   if (!d->root) {
      ramfs_err_case_destroy(fs);
      return NULL;
   }

   return fs;
}

