/* SPDX-License-Identifier: BSD-2-Clause */

static struct sysfs_inode *
sysfs_new_inode(struct sysfs_data *d)
{
   struct sysfs_inode *i = kzalloc_obj(struct sysfs_inode);

   if (!i)
      return NULL;

   i->ino = d->next_inode++;
   return i;
}

static void
sysfs_destroy_inode(struct sysfs_data *d, struct sysfs_inode *i)
{
   ASSERT(get_ref_count(i) == 0);

   if (i->type == VFS_SYMLINK)
      kfree2(i->symlink.path, i->symlink.path_len);

   kfree_obj(i, struct sysfs_inode);
}

static struct sysfs_inode *
sysfs_create_inode_dir(struct sysfs_data *d,
                       struct sysfs_inode *parent)
{
   struct sysfs_inode *i = sysfs_new_inode(d);

   if (!i)
      return NULL;

   i->type = VFS_DIR;
   list_init(&i->dir.entries_list);

   if (!parent)
      parent = i;    /* root case */

   if (sysfs_dir_add_entry(i, ".", i, NULL) < 0) {
      kfree_obj(i, struct sysfs_inode);
      return NULL;
   }

   if (sysfs_dir_add_entry(i, "..", parent, NULL) < 0) {

      struct sysfs_entry *e =
         list_first_obj(&i->dir.entries_list,
                        struct sysfs_entry,
                        lnode);

      sysfs_dir_remove_entry(i, e);
      kfree_obj(i, struct sysfs_inode);
      return NULL;
   }

   return i;
}

static struct sysfs_inode *
sysfs_create_inode_file(struct sysfs_data *d,
                        struct sysfs_inode *parent,
                        int prop_idx)
{
   ASSERT(parent->type == VFS_DIR);

   struct sysfs_inode *i = sysfs_new_inode(d);
   struct sysobj *obj = parent->dir.obj;
   void **prop_data_arr = obj->prop_data;

   if (!i)
      return NULL;

   i->type = VFS_FILE;
   i->file.obj = obj;

   if (obj->type) {
      i->file.prop = obj->type->properties[prop_idx];
      i->file.prop_data = prop_data_arr[prop_idx];
   }

   return i;
}

static struct sysfs_inode *
sysfs_create_inode_symlink(struct sysfs_data *d,
                           struct sysfs_inode *parent,
                           const char *target)
{
   struct sysfs_inode *i = sysfs_new_inode(d);
   char *path;
   size_t pl;

   if (!i)
      return NULL;

   pl = strlen(target);

   if (!(path = kmalloc(pl + 1))) {
      sysfs_destroy_inode(d, i);
      return NULL;
   }

   memcpy(path, target, pl + 1);
   i->type = VFS_SYMLINK;
   i->symlink.path_len = (u32)pl;
   i->symlink.path = path;
   return i;
}
