/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/fs/flock.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/bintree.h>

struct locked_file {

   REF_COUNTED_OBJECT;

   struct bintree_node node;
   enum subsystem owner;

   struct fs *fs;
   vfs_inode_ptr_t inode;
};

int
acquire_subsystem_file_exlock(struct fs *fs,
                              vfs_inode_ptr_t i,
                              enum subsystem subsys,
                              struct locked_file **lock_ref)
{
   struct locked_file *lf;
   int rc;

   disable_preemption();
   {
      lf = bintree_find_ptr(fs->pss_lock_root,
                            i,
                            struct locked_file,
                            node,
                            inode);

      if (lf) {

         if (lf->owner == subsys) {
            *lock_ref = lf;         /* the subsystem already owns the exlock */
            retain_obj(lf);         /* retain the locked_file object */
            enable_preemption();
            return 0;
         }

         enable_preemption();
         return -ETXTBSY;           /* the lock is owned by another subsystem */
      }
   }
   enable_preemption();

   /* There's no such lock object: let's try to grab the real lock */
   rc = vfs_exlock_noblock(fs, i);

   if (rc < 0 && UNLIKELY(rc != -ENOLCK)) {

      /*
       * Too bad: something else holds the lock.
       *
       * NOTE: this means we're accepting accepting the case where the call
       * failed with -ENOLCK, meaning that no per-file locking mechanism has
       * been implemented at fs-level. That's fine: it just means that the FS
       * cannot be a network FS, with its own locking logic beyond the current
       * machine. In this case, only the per-subsystem locking offered by this
       * code will be available.
       */
      return rc;
   }

   /* We've got it. Great! */
   lf = kzmalloc(sizeof(struct locked_file));

   if (UNLIKELY(!lf)) {

      /* Oops, out of memory! */
      vfs_exunlock(fs, i);
      return -ENOMEM;
   }

   /* We're cool: setup the lock object and append it to the right list */
   lf->owner = subsys;
   lf->fs = fs;
   lf->inode = i;
   retain_obj(lf);
   retain_obj(lf->fs);

   disable_preemption();
   {
      bintree_insert_ptr(&fs->pss_lock_root,
                         lf,
                         struct locked_file,
                         node,
                         inode);
   }
   enable_preemption();

   *lock_ref = lf;
   return 0;
}

void
retain_subsystem_file_exlock(struct locked_file *lf)
{
   retain_obj(lf);
}

void
release_subsystem_file_exlock(struct locked_file *lf)
{
   const int curr_ref_count = release_obj(lf);
   int rc;

   if (curr_ref_count > 0)
      return; /* that's it */

   /*
    * We have to destroy the lock object, but first we have to release the
    * actual per-file lock.
    */
   rc = vfs_exunlock(lf->fs, lf->inode);

   if (rc < 0 && UNLIKELY(rc != -ENOLCK)) {

      /*
       * Weird: something went wrong while trying to release the lock.
       * This MUST never happen, if the `lf` is a valid object, returned by
       * acquire_subsystem_file_exlock(), after acquiring the per-lock.
       * If we got here, there's a bug in filesystem's exlock/exunlock impl.
       *
       * NOTE: we're accepting accepting the case where the call failed with
       * -ENOLCK. See the comments in acquire_subsystem_file_exlock().
       */

      panic("Failed to release per-file lock with: %d", rc);
   }

   /* Now, remove the lock object from the list */
   disable_preemption();
   {
      bintree_remove_ptr(&lf->fs->pss_lock_root,
                         lf,
                         struct locked_file,
                         node,
                         inode);
   }
   enable_preemption();

   /* Release `lf->fs` and destroy the `lf` object itself */
   release_obj(lf->fs);
   kfree2(lf, sizeof(struct locked_file));
}

int
acquire_subsystem_file_exlock_h(fs_handle h,
                                enum subsystem subsys,
                                struct locked_file **lock_ref)
{
   struct fs_handle_base *hb = h;
   struct fs *fs = hb->fs;
   vfs_inode_ptr_t i = fs->fsops->get_inode(hb);

   if (!i)
      return -EBADF;

   return acquire_subsystem_file_exlock(fs, i, subsys, lock_ref);
}
