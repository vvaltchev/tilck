/* SPDX-License-Identifier: BSD-2-Clause */

static long sysfs_insert_remove_entry_cmp(const void *a, const void *b)
{
   const struct sysfs_entry *e1 = a;
   const struct sysfs_entry *e2 = b;
   return strcmp(e1->name, e2->name);
}

static long sysfs_find_entry_cmp(const void *obj, const void *valptr)
{
   const struct sysfs_entry *e = obj;
   const char *searched_str = valptr;
   return strcmp(e->name, searched_str);
}


static int
sysfs_getdents(fs_handle h, get_dents_func_cb cb, void *arg)
{
   struct sysfs_handle *sh = h;
   struct sysfs_inode *inode = sh->inode;
   int rc = 0;

   if (inode->type != VFS_DIR)
      return -ENOTDIR;

   list_for_each_ro_kp(sh->dir.dpos, &inode->dir.entries_list, lnode) {

      struct vfs_dent64 dent = {
         .ino        = sh->dir.dpos->inode->ino,
         .type       = sh->dir.dpos->inode->type,
         .name_len   = sh->dir.dpos->name_len,
         .name       = sh->dir.dpos->name,
      };

      if ((rc = cb(&dent, arg)))
         break;
   }

   return rc;
}

static struct sysfs_entry *
sysfs_dir_get_entry_by_name(struct sysfs_inode *idir,
                            const char *name,
                            ssize_t len)
{
   char buf[SYSFS_ENTRY_MAX_LEN];
   memcpy(buf, name, (size_t) len);
   buf[len] = 0;

   return bintree_find(idir->dir.entries_tree_root,
                       buf,
                       sysfs_find_entry_cmp,
                       struct sysfs_entry,
                       node);
}

static void
sysfs_get_entry(struct fs *fs,
                void *dir_inode,
                const char *name,
                ssize_t name_len,
                struct fs_path *fs_path)
{
   struct sysfs_data *d = fs->device_data;
   struct sysfs_inode *idir = dir_inode;
   struct sysfs_entry *e;

   if (!dir_inode) {

      *fs_path = (struct fs_path) {
         .inode = d->root,
         .dir_inode = d->root,
         .dir_entry = NULL,
         .type = VFS_DIR,
      };

      return;
   }

   e = sysfs_dir_get_entry_by_name(idir, name, name_len);

   *fs_path = (struct fs_path) {
      .inode      = e ? e->inode : NULL,
      .dir_inode  = idir,
      .dir_entry  = e,
      .type       = e ? e->inode->type : VFS_NONE,
   };
}


static int
sysfs_dir_add_entry(struct sysfs_inode *idir,
                    const char *iname,
                    struct sysfs_inode *ie,
                    struct sysfs_entry **entry_ref)
{
   struct sysfs_entry *e;
   size_t enl = strlen(iname) + 1;
   bool success;

   ASSERT(idir->type == VFS_DIR);

   if (enl == 1)
      return -ENOENT;

   if (enl > sizeof(e->name))
      return -ENAMETOOLONG;

   if (!(e = kalloc_obj(struct sysfs_entry)))
      return -ENOSPC;

   list_node_init(&e->lnode);
   bintree_node_init(&e->node);

   e->inode = ie;
   memcpy(e->name, iname, enl);

   if (e->name[enl-2] == '/') {
      e->name[enl-2] = 0; /* drop the trailing slash */
      enl--;
   }

   e->name_len = (u8) enl;

   success =
      bintree_insert(&idir->dir.entries_tree_root,
                     e,
                     sysfs_insert_remove_entry_cmp,
                     struct sysfs_entry,
                     node);

   if (!success) {
      kfree_obj(e, struct sysfs_entry);
      return -EEXIST;
   }

   list_add_tail(&idir->dir.entries_list, &e->lnode);
   idir->dir.num_entries++;

   if (entry_ref)
      *entry_ref = e;

   return 0;
}

static void
sysfs_dir_remove_entry(struct sysfs_inode *idir, struct sysfs_entry *e)
{
   ASSERT(idir->type == VFS_DIR);

   bintree_remove(&idir->dir.entries_tree_root,
                  e,
                  sysfs_insert_remove_entry_cmp,
                  struct sysfs_entry,
                  node);

   list_remove(&e->lnode);
   idir->dir.num_entries--;
   kfree_obj(e, struct sysfs_entry);
}
