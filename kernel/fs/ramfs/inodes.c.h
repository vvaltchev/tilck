/* SPDX-License-Identifier: BSD-2-Clause */

static ramfs_inode *ramfs_new_inode(ramfs_data *d)
{
   ramfs_inode *i = kzmalloc(sizeof(ramfs_inode));

   if (!i)
      return NULL;

   rwlock_wp_init(&i->rwlock);
   i->inode = d->next_inode_num++;
   return i;
}

static ramfs_inode *
ramfs_create_inode_dir(ramfs_data *d, mode_t mode, ramfs_inode *parent)
{
   ramfs_inode *i = ramfs_new_inode(d);

   if (!i)
      return NULL;

   i->type = RAMFS_DIRECTORY;
   i->mode = (mode & 0777) | S_IFDIR;

   list_init(&i->entries_list);

   if (ramfs_dir_add_entry(i, ".", i) < 0) {
      kfree2(i, sizeof(ramfs_inode));
      return NULL;
   }

   if (!parent)
      parent = i;

   if (ramfs_dir_add_entry(i, "..", parent) < 0) {

      ramfs_entry *e = list_first_obj(&i->entries_list, ramfs_entry, node);
      ramfs_dir_remove_entry(i, e);

      kfree2(i, sizeof(ramfs_inode));
      return NULL;
   }

   read_system_clock_datetime(&i->ctime);
   i->wtime = i->ctime;
   return i;
}

static ramfs_inode *
ramfs_create_inode_file(ramfs_data *d, mode_t mode, ramfs_inode *parent)
{
   ramfs_inode *i = ramfs_new_inode(d);

   if (!i)
      return NULL;

   i->type = RAMFS_FILE;
   i->mode = (mode & 0777) | S_IFREG;

   read_system_clock_datetime(&i->ctime);
   i->wtime = i->ctime;
   return i;
}

static int ramfs_destroy_inode(ramfs_data *d, ramfs_inode *i)
{
   if (i->type == RAMFS_DIRECTORY) {

      if (!list_is_empty(&i->entries_list))
         return -ENOTEMPTY;
   }

   kfree2(i, sizeof(ramfs_inode));
   return 0;
}
