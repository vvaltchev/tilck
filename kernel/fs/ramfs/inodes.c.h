/* SPDX-License-Identifier: BSD-2-Clause */

#define DEBUG_RAMFS_CREATE_INODE_PRINTK      0

static struct ramfs_inode *ramfs_new_inode(struct ramfs_data *d)
{
   struct ramfs_inode *i = kzmalloc(sizeof(struct ramfs_inode));

   if (!i)
      return NULL;

   rwlock_wp_init(&i->rwlock, true);
   list_init(&i->mappings_list);

   i->type = VFS_NONE;
   i->ino = d->next_inode_num++;

   if (DEBUG_RAMFS_CREATE_INODE_PRINTK) {
      printk("[ramfs] Create inode with ref_count at %p\n", &i->ref_count);
   }

   return i;
}

static struct ramfs_inode *
ramfs_create_inode_dir(struct ramfs_data *d, mode_t mode, struct ramfs_inode *parent)
{
   struct ramfs_inode *i = ramfs_new_inode(d);

   if (!i)
      return NULL;

   i->type = VFS_DIR;
   i->mode = (mode & 0777) | S_IFDIR;
   list_init(&i->entries_list);
   list_init(&i->handles_list);

   if (!parent) {
      /* root case */
      parent = i;
   }

   i->parent_dir = parent;

   if (ramfs_dir_add_entry(i, ".", i) < 0) {
      kfree2(i, sizeof(struct ramfs_inode));
      return NULL;
   }

   if (ramfs_dir_add_entry(i, "..", parent) < 0) {

      struct ramfs_entry *e = i->entries_tree_root;
      ramfs_dir_remove_entry(i, e);

      kfree2(i, sizeof(struct ramfs_inode));
      return NULL;
   }

   i->ctime = read_system_clock_timestamp();
   i->mtime = i->ctime;
   return i;
}

static struct ramfs_inode *
ramfs_create_inode_file(struct ramfs_data *d, mode_t mode, struct ramfs_inode *parent)
{
   struct ramfs_inode *i = ramfs_new_inode(d);

   if (!i)
      return NULL;

   i->type = VFS_FILE;
   i->mode = (mode & 0777) | S_IFREG;

   i->parent_dir = parent;
   i->ctime = read_system_clock_timestamp();
   i->mtime = i->ctime;
   return i;
}

static int ramfs_destroy_inode(struct ramfs_data *d, struct ramfs_inode *i)
{
   /*
    * We can destroy only inodes referring to NO blocks (= data) in case of
    * files and no entries in case of directories. Also, we have to be SURE that
    * no dir entry nor file handle points to it.
    */
   ASSERT(get_ref_count(i) == 0);
   ASSERT(i->nlink == 0);

   switch (i->type) {

      case VFS_NONE:
         /* do nothing */
         break;

      case VFS_FILE:
         ASSERT(i->blocks_tree_root == NULL);
         break;

      case VFS_DIR:
         ASSERT(i->entries_tree_root == NULL);
         break;

      case VFS_SYMLINK:
         kfree2(i->path, i->path_len + 1);
         break;

      default:
         NOT_IMPLEMENTED();
   }

   rwlock_wp_destroy(&i->rwlock);
   kfree2(i, sizeof(struct ramfs_inode));
   return 0;
}

static struct ramfs_inode *
ramfs_create_inode_symlink(struct ramfs_data *d,
                           struct ramfs_inode *parent,
                           const char *target)
{
   struct ramfs_inode *i = ramfs_new_inode(d);
   char *path;
   size_t pl;

   if (!i)
      return NULL;

   pl = strlen(target);

   if (!(path = kmalloc(pl + 1))) {
      ramfs_destroy_inode(d, i);
      return NULL;
   }

   memcpy(path, target, pl + 1);

   i->type = VFS_SYMLINK;
   i->mode = 0777 | S_IFLNK;
   i->parent_dir = parent;
   i->path_len = pl;
   i->path = path;
   i->ctime = read_system_clock_timestamp();
   i->mtime = i->ctime;
   return i;
}
