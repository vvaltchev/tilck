/* SPDX-License-Identifier: BSD-2-Clause */

static sptr ramfs_insert_remove_entry_cmp(const void *a, const void *b)
{
   const struct ramfs_entry *e1 = a;
   const struct ramfs_entry *e2 = b;
   return strcmp(e1->name, e2->name);
}

static sptr ramfs_find_entry_cmp(const void *obj, const void *valptr)
{
   const struct ramfs_entry *e = obj;
   const char *searched_str = valptr;
   return strcmp(e->name, searched_str);
}

static int
ramfs_dir_add_entry(struct ramfs_inode *idir,
                    const char *iname,
                    struct ramfs_inode *ie)
{
   struct ramfs_entry *e;
   size_t enl = strlen(iname) + 1;
   ASSERT(idir->type == VFS_DIR);

   if (enl > sizeof(e->name))
      return -ENAMETOOLONG;

   if (!(e = kmalloc(sizeof(struct ramfs_entry))))
      return -ENOSPC;

   ASSERT(ie->parent_dir != NULL);

   bintree_node_init(&e->node);
   list_node_init(&e->lnode);

   e->inode = ie;
   memcpy(e->name, iname, enl);

   if (e->name[enl-2] == '/') {
      e->name[enl-2] = 0; /* drop the trailing slash */
      enl--;
   }

   e->name_len = (u8) enl;

   bintree_insert(&idir->entries_tree_root,
                  e,
                  ramfs_insert_remove_entry_cmp,
                  struct ramfs_entry,
                  node);

   list_add_tail(&idir->entries_list, &e->lnode);

   ie->nlink++;
   idir->num_entries++;
   return 0;
}

static void
ramfs_dir_remove_entry(struct ramfs_inode *idir, struct ramfs_entry *e)
{
   struct ramfs_handle *pos;
   struct ramfs_inode *ie = e->inode;
   ASSERT(idir->type == VFS_DIR);

   /*
    * Before removing this entry, we have to check if, among the handles opened
    * for `idir`, there are any having dpos == e. For each one of them, we have
    * to move `dpos` forward, before removing the entry `e`.
    */

   list_for_each_ro(pos, &idir->handles_list, node) {

      if (pos->dpos == e)
         pos->dpos = list_next_obj(pos->dpos, lnode);
   }

   bintree_remove(&idir->entries_tree_root,
                  e,
                  ramfs_insert_remove_entry_cmp,
                  struct ramfs_entry,
                  node);

   list_remove(&e->lnode);

   ASSERT(ie->nlink > 0);
   ie->nlink--;
   idir->num_entries--;
   kfree2(e, sizeof(struct ramfs_entry));
}

static struct ramfs_entry *
ramfs_dir_get_entry_by_name(struct ramfs_inode *idir,
                            const char *name,
                            ssize_t len)
{
   char buf[RAMFS_ENTRY_MAX_LEN];
   memcpy(buf, name, (size_t) len);
   buf[len] = 0;

   return bintree_find(idir->entries_tree_root,
                       buf,
                       ramfs_find_entry_cmp,
                       struct ramfs_entry,
                       node);
}
