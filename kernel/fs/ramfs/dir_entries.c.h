/* SPDX-License-Identifier: BSD-2-Clause */

static int
ramfs_dir_add_entry(ramfs_inode *idir, const char *iname, ramfs_inode *ie)
{
   ramfs_entry *e;
   const size_t enl = strlen(iname) + 1;
   ASSERT(idir->type == RAMFS_DIRECTORY);

   if (enl > sizeof(e->name))
      return -ENAMETOOLONG;

   if (!(e = kmalloc(sizeof(ramfs_entry))))
      return -ENOMEM;

   list_node_init(&e->node);
   e->inode = ie;
   memcpy(e->name, iname, enl);

   rwlock_wp_exlock(&idir->rwlock);
   {
      list_add_tail(&idir->entries_list, &e->node);
   }
   rwlock_wp_exunlock(&idir->rwlock);
   retain_obj(ie);
   return 0;
}

static void
ramfs_dir_remove_entry(ramfs_inode *idir, ramfs_entry *e)
{
   ramfs_inode *ie = e->inode;
   ASSERT(idir->type == RAMFS_DIRECTORY);

   rwlock_wp_exlock(&idir->rwlock);
   {
      list_remove(&e->node);
   }
   rwlock_wp_exunlock(&idir->rwlock);
   release_obj(ie);
   kfree2(e, sizeof(ramfs_entry));
}

static ramfs_entry *
ramfs_dir_get_entry_by_name(ramfs_inode *idir, const char *name, ssize_t len)
{
   ramfs_entry *pos;

   list_for_each_ro(pos, &idir->entries_list, node) {
      if (!strncmp(pos->name, name, (size_t) len))
         return pos;
   }

   return NULL;
}

static ssize_t ramfs_dir_read(fs_handle h, char *buf, size_t len)
{
   return -EINVAL;
}

static ssize_t ramfs_dir_write(fs_handle h, char *buf, size_t len)
{
   return -EINVAL;
}

static off_t ramfs_dir_seek(fs_handle h, off_t offset, int whence)
{
   return -EINVAL;
}

static int ramfs_dir_ioctl(fs_handle h, uptr request, void *arg)
{
   return -EINVAL;
}

static int ramfs_dir_fcntl(fs_handle h, int cmd, int arg)
{
   return -EINVAL;
}
