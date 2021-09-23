/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/sched.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/datetime.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/rwlock.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/modules.h>
#include <tilck/kernel/sort.h>

#include "sysfs_int.h"
#include "dents.c.h"
#include "inodes.c.h"
#include "dirops.c.h"
#include "fileops.c.h"
#include "types.c.h"
#include "lock_and_retain.c.h"

void sysfs_create_config_obj(void);
static struct fs *sysfs;

static int
sysfs_open(struct vfs_path *p, fs_handle *out, int fl, mode_t mod)
{
   struct sysfs_path *sp = (struct sysfs_path *) &p->fs_path;
   const struct sysobj_prop_type *pt;

   if (sp->inode) {

      if (sp->type == VFS_DIR)
         return sysfs_open_dir(p->fs, sp->inode, out);

      ASSERT(sp->type == VFS_FILE);

      if ((fl & O_CREAT) && (fl & O_EXCL))
         return -EEXIST;

      pt = sp->inode->file.prop->type;

      if (~fl & O_WRONLY)
         if (!pt || !pt->load)
            return -EACCES;

      if (fl & (O_WRONLY | O_RDWR))
         if (!pt || !pt->store)
            return -EACCES;

      return sysfs_open_file(p->fs, sp->inode, out);
   }

   if (fl & O_CREAT)
      return -EROFS;

   return -ENOENT;
}

static vfs_inode_ptr_t
sysfs_get_inode(fs_handle h)
{
   struct sysfs_handle *sh = h;
   return sh->inode;
}

static int
sysfs_stat(struct fs *fs, vfs_inode_ptr_t i, struct k_stat64 *statbuf)
{
   struct sysfs_inode *inode = i;
   struct sysfs_data *d = fs->device_data;
   const struct sysobj_prop_type *pt;

   bzero(statbuf, sizeof(struct k_stat64));

   switch (inode->type) {

      case VFS_FILE:

         pt = inode->file.prop->type;
         statbuf->st_mode = S_IFREG;

         if (pt) {

            if (pt->load)
               statbuf->st_mode |= 0400;

            if (pt->store)
               statbuf->st_mode |= 0200;

            if (pt->get_buf_sz) {
               statbuf->st_size = (typeof(statbuf->st_size)) ABS(
                  pt->get_buf_sz(inode->file.obj, inode->file.prop_data)
               );
            }
         }

         break;

      case VFS_DIR:
         statbuf->st_mode = 0555 | S_IFDIR;
         statbuf->st_size = (typeof(statbuf->st_size))
            (inode->dir.num_entries * (offt) sizeof(struct sysfs_entry));
         break;

      case VFS_SYMLINK:
         statbuf->st_mode = 0777 | S_IFLNK;
         statbuf->st_size = (typeof(statbuf->st_size)) inode->symlink.path_len;
         break;

      default:
         NOT_IMPLEMENTED();
   }

   statbuf->st_dev = fs->device_id;
   statbuf->st_ino = inode->ino;
   statbuf->st_nlink = 1;
   statbuf->st_uid = 0; /* root */
   statbuf->st_gid = 0; /* root */
   statbuf->st_blksize = PAGE_SIZE;
   statbuf->st_blocks = 0;
   statbuf->st_ctim.tv_sec = d->wrt_time;
   statbuf->st_mtim = statbuf->st_ctim;
   statbuf->st_atim = statbuf->st_mtim;

   return 0;
}

/* NOTE: `buf` is guaranteed to have room for at least MAX_PATH chars */
static int sysfs_readlink(struct vfs_path *p, char *buf)
{
   struct sysfs_inode *i = p->fs_path.inode;

   if (i->type != VFS_SYMLINK)
      return -EINVAL;

   memcpy(buf, i->symlink.path, i->symlink.path_len); /* skip final \0 */
   return (int) i->symlink.path_len;
}

static void
sysfs_syncfs(struct fs *fs)
{
   struct sysfs_data *d = fs->device_data;
   struct sysfs_handle *h;

   while (true) {

      h = NULL;

      disable_preemption();
      {
         if (!list_is_empty(&d->dirty_handles)) {

            h = list_first_obj(&d->dirty_handles,
                               struct sysfs_handle,
                               file.dirty_node);

            list_remove(&h->file.dirty_node);
            list_node_init(&h->file.dirty_node);
         }
      }
      enable_preemption();

      if (!h)
         break;

      sysfs_fsync(h);
   }
}

static const struct fs_ops static_fsops_sysfs =
{
   .get_inode = sysfs_get_inode,
   .open = sysfs_open,
   .on_close = sysfs_on_close,
   .on_dup_cb = sysfs_on_dup,
   .getdents = sysfs_getdents,
   .unlink = NULL,
   .mkdir = NULL,
   .rmdir = NULL,
   .truncate = NULL,
   .stat = sysfs_stat,
   .symlink = NULL,
   .readlink = sysfs_readlink,
   .chmod = NULL,
   .get_entry = sysfs_get_entry,
   .rename = NULL,
   .link = NULL,
   .retain_inode = sysfs_retain_inode,
   .release_inode = sysfs_release_inode,
   .syncfs = sysfs_syncfs,

   .fs_exlock = sysfs_exclusive_lock,
   .fs_exunlock = sysfs_exclusive_unlock,
   .fs_shlock = sysfs_shared_lock,
   .fs_shunlock = sysfs_shared_unlock,
};

static u32
sysobj_type_get_prop_count(struct sysobj_type *type)
{
   struct sysobj_prop **prop_ptr = type->properties;
   u32 count = 0;

   while (*prop_ptr) {
      count++; prop_ptr++;
   }

   return count;
}

static struct sysobj *
__sysobj_init(struct sysobj *obj,
              struct sysobj_type *type,
              struct sysobj_hooks *hooks,
              void **prop_data,
              bool prop_data_owned,
              bool type_owned)
{
   list_node_init(&obj->node);
   list_init(&obj->children_list);

   obj->type = type;
   obj->hooks = hooks;
   obj->prop_data = prop_data;
   obj->extra = NULL;
   obj->prop_data_owned = prop_data_owned;
   obj->type_owned = type_owned;
   obj->object_owned = true;        /* See sysobj_init() */
   obj->inode = NULL;
   obj->entry = NULL;
   obj->parent = NULL;
   return obj;
}

void
sysobj_init(struct sysobj *obj,
            struct sysobj_type *type,
            struct sysobj_hooks *hooks,
            void **prop_data)
{
   ASSERT(obj != NULL);
   ASSERT(type != NULL);

   __sysobj_init(obj, type, hooks, prop_data, false, false);

   /*
    * The only case (in this file) where the object is NOT owned by sysfs
    * because the caller created it.
    */
   obj->object_owned = false;
}

struct sysobj *
sysfs_create_obj_va_arr(struct sysobj_type *type,
                        struct sysobj_hooks *hooks,
                        void **prop_data)
{
   struct sysobj *obj;

   if (!(obj = kalloc_obj(struct sysobj)))
      return NULL;

   return __sysobj_init(obj, type, hooks, prop_data, false, false);
}

struct sysobj *
sysfs_create_obj_va(struct sysobj_type *type,
                    struct sysobj_hooks *hooks,
                    va_list args)
{
   void **dyn_prop_data;
   struct sysobj *obj;
   u32 i, prop_cnt;

   prop_cnt = sysobj_type_get_prop_count(type);
   dyn_prop_data = kzalloc_array_obj(void *, prop_cnt);  /* No trailing NULL */

   if (!dyn_prop_data)
      return NULL;

   for (i = 0; i < prop_cnt; i++)
      dyn_prop_data[i] = va_arg(args, void *);

   if (!(obj = sysfs_create_obj_va_arr(type, hooks, dyn_prop_data)))
      kfree_array_obj(dyn_prop_data, void *, prop_cnt); /* No trailing NULL */

   /*
    * We have to set prop_data_owned because we (sysfs) allocated the prop_data
    * array. When the sys object is destroyed, we (the sysfs layer) have the
    * responsability to free that array.
    */
   obj->prop_data_owned = true;
   return obj;
}

struct sysobj *
sysfs_create_obj(struct sysobj_type *type, struct sysobj_hooks *hooks, ...)
{
   struct sysobj *ret;
   va_list args;

   va_start(args, hooks);
   ret = sysfs_create_obj_va(type, hooks, args);
   va_end(args);
   return ret;
}

struct sysobj *
sysfs_create_custom_obj_va(const char *type_name,
                           struct sysobj_hooks *hooks,
                           u32 args_cnt,
                           va_list args)
{
   const u32 prop_cnt = args_cnt / 2;
   struct sysobj_type *ot;
   struct sysobj *obj;
   void **dyn_prop_data;

   if (args_cnt % 2) {
      /* Expects N <prop, value> pairs. Cannot have an odd number of args. */
      return NULL;
   }

   if (!(obj = kalloc_obj(struct sysobj)))
      return NULL;

   if (!(ot = kalloc_obj(struct sysobj_type))) {
      kfree_obj(obj, struct sysobj);
      return NULL;
   }

   ot->name = type_name;
   ot->properties = kzalloc_array_obj(struct sysobj_prop *, prop_cnt + 1);

   if (!ot->properties) {
      kfree_obj(ot, struct sysobj);
      kfree_obj(obj, struct sysobj_type);
      return NULL;
   }

   dyn_prop_data = kzalloc_array_obj(void *, prop_cnt); /* No trailing NULL */

   if (!dyn_prop_data) {
      kfree_array_obj(ot->properties, struct sysobj_prop *, prop_cnt + 1);
      kfree_obj(ot, struct sysobj);
      kfree_obj(obj, struct sysobj_type);
      return NULL;
   }

   for (u32 i = 0; i < prop_cnt; i++) {
      ot->properties[i] = va_arg(args, void *);
      dyn_prop_data[i] = va_arg(args, void *);
   }

   return __sysobj_init(obj, ot, hooks, dyn_prop_data, true, true);
}

struct sysobj *
sysfs_create_custom_obj(const char *type_name, struct sysobj_hooks *hooks, ...)
{
   struct sysobj *ret;
   va_list args;
   u32 cnt = 0;

   va_start(args, hooks);

   /*
    * Count the number of varargs (expected to be in <prop, data> pairs) until
    * the `prop` arg is NOT null. For example:
    *
    *    sysfs_create_custom_obj("my_type1",
    *                            NULL,           // hooks
    *                            prop1, data1,
    *                            prop2, NULL,
    *                            prop3, data3,
    *                            NULL);
    *
    * Is a valid use case for this function. The `prop` element of the pair
    * must always be != NULL, but the data part can be NULL instead.
    */

   while (va_arg(args, void *) || (cnt % 2))
      cnt++;

   va_end(args);

   va_start(args, hooks);
   ret = sysfs_create_custom_obj_va(type_name, hooks, cnt, args);
   va_end(args);
   return ret;
}

static int
sysfs_create_files_for_obj(struct fs *fs, struct sysobj *obj)
{
   struct sysobj_prop **ptr, *prop;
   struct sysfs_inode *i;
   int rc, idx = 0;

   if (!obj->type)
      return 0;

   for (ptr = &obj->type->properties[0]; *ptr != NULL; ptr++, idx++) {

      prop = *ptr;
      i = sysfs_create_inode_file(fs->device_data, obj->inode, idx);

      if (!i)
         return -ENOMEM; /* NOTE: don't rollback everything */

      rc = sysfs_dir_add_entry(obj->inode, prop->name, i, NULL);

      if (rc)
         return rc; /* NOTE: don't rollback everything */
   }

   return 0;
}

void
sysfs_destroy_unregistered_obj(struct sysobj *obj)
{
   ASSERT(obj);
   ASSERT(!obj->inode);
   ASSERT(obj->object_owned);

   if (obj->prop_data_owned) {
      kfree(obj->prop_data);
   }

   if (obj->type_owned) {
      kfree(obj->type->properties);
      kfree(obj->type);
   }

   kfree_obj(obj, struct sysobj);
}

int
sysfs_register_obj(struct fs *fs,
                   struct sysobj *parent,
                   const char *name,
                   struct sysobj *obj)
{
   struct sysfs_inode *iobj, *iparent;
   struct sysfs_data *d;
   int rc;

   if (!fs) {
      ASSERT(sysfs != NULL);
      fs = sysfs;
   }

   d = fs->device_data;
   ASSERT(obj != NULL);

   if (!parent) {

      if (d->root->dir.obj)
         return -EEXIST;

      if (name)
         return -EINVAL;

      iobj = d->root;

   } else {

      iparent = parent->inode;

      if (!iparent)
         return -EINVAL;   /* the parent sysobj is not registered */

      if (!(iobj = sysfs_create_inode_dir(d, iparent)))
         return -ENOMEM;

      if ((rc = sysfs_dir_add_entry(iparent, name, iobj, &obj->entry))) {
         sysfs_destroy_inode(d, iobj);
         return rc;
      }

      list_add_tail(&parent->children_list, &obj->node);
   }

   iobj->dir.obj = obj;
   obj->inode = iobj;
   obj->parent = parent;
   return sysfs_create_files_for_obj(fs, obj);
}

struct symlink_tmp {

   char path[MAX_PATH];
   struct sysobj *np_path[96];      /* root to `new_parent` path */
   struct sysobj *obj_path[96];     /* root to `obj` path */
};

static int
sysfs_get_path_to_root(struct sysobj *obj, struct sysobj **path, int pl)
{
   int i;

   for (i = 0; obj->parent != NULL && i < pl; obj = obj->parent, i++)
      path[i] = obj;

   if (obj->parent)
      return -ENAMETOOLONG;

   array_reverse_ptr(path, (u32)i);
   return i;
}

int
sysfs_symlink_obj(struct fs *fs,
                  struct sysobj *new_parent,
                  const char *new_name,
                  struct sysobj *obj)
{
   struct symlink_tmp *tmp;
   struct sysfs_inode *link;
   struct sysfs_data *sd;
   struct sysobj *n;
   int rc, np_nodes, obj_nodes, len;
   int i, past_lca; /* index of the lowest common ancestor + 1 */
   char *path, *path_end;

   ASSERT(new_parent);
   ASSERT(new_name);
   ASSERT(obj);

   if (!fs) {
      /* The main sysfs must be initialized */
      ASSERT(sysfs != NULL);
      fs = sysfs;
   }

   sd = fs->device_data;

   if (!(tmp = kzalloc_obj(struct symlink_tmp)))
      goto oom;

   path = tmp->path;
   path_end = tmp->path + sizeof(tmp->path);

   rc = sysfs_get_path_to_root(new_parent,
                               tmp->np_path,
                               ARRAY_SIZE(tmp->np_path));

   if (rc < 0)
      goto out;

   np_nodes = rc;
   rc = sysfs_get_path_to_root(obj,
                               tmp->obj_path,
                               ARRAY_SIZE(tmp->obj_path));

   if (rc < 0)
      goto out;

   obj_nodes = rc;
   len = MIN(np_nodes, obj_nodes);
   i = 0;

   while (i < len && tmp->np_path[i] == tmp->obj_path[i]) {
      i++;
   }

   past_lca = i;

   /* Append "../" to the path until it reaches our LCA */
   for (i = past_lca; i < np_nodes; i++) {

      if (path_end - path <= 3)
         goto nametoolong;

      *path++ = '.';
      *path++ = '.';
      *path++ = '/';
   }

   /* Now append the path from LCA to our object */
   for (i = past_lca; i < obj_nodes; i++) {

      n = tmp->obj_path[i];

      if (path_end - path <= n->entry->name_len)
         goto nametoolong;

      memcpy(path, n->entry->name, n->entry->name_len - 1);
      path += n->entry->name_len - 1;
      *path++ = '/';
   }

   if (path == tmp->path) {
      /* Path is empty, meaning `new_parent` == `obj`: corner case */
      *path++ = '.';
   }

   /* `path` always points to the next char to write: go back by 1 */
   path--;

   if (*path == '/')
      *path = 0; /* Drop the trailing '/' */

   /* Create the symlink inode using `path` as target */
   link = sysfs_create_inode_symlink(sd, new_parent->inode, tmp->path);

   if (!link)
      goto oom;

   /* Now finally create a dir entry for the symlink */
   rc = sysfs_dir_add_entry(new_parent->inode, new_name, link, NULL);

   if (rc < 0)
      sysfs_destroy_inode(sd, link);

out:
   /* Free our temporary object, since `link` now has a copy of tmp->path */
   kfree_obj(tmp, struct symlink_tmp);
   return rc;

nametoolong:
   rc = -ENAMETOOLONG;
   goto out;

oom:
   rc = -ENOMEM;
   goto out;
}

struct fs *
create_sysfs(void)
{
   struct fs *fs;
   struct sysfs_data *d;

   if (!(d = kzalloc_obj(struct sysfs_data)))
      return NULL;

   fs = create_fs_obj("sysfs", &static_fsops_sysfs, d, VFS_FS_RW);

   if (!fs) {
      kfree_obj(d, struct sysfs_data);
      return NULL;
   }

   d->next_inode = 1;
   d->wrt_time = (time_t)get_timestamp();
   rwlock_wp_init(&d->rwlock, false);
   list_init(&d->dirty_handles);
   d->root = sysfs_create_inode_dir(d, NULL);

   if (!d->root) {
      rwlock_wp_destroy(&d->rwlock);
      kfree_obj(d, struct sysfs_data);
      destory_fs_obj(fs);
      return NULL;
   }

   return fs;
}

struct sysobj *
sysfs_create_empty_obj(void)
{
   return sysfs_create_obj_va_arr(NULL,   /* type */
                                  NULL,   /* hooks */
                                  NULL);  /* prop data */
}

DEF_SHARED_EMPTY_SYSOBJ(sysfs_root_obj);      /* sysfs path: /   */
DEF_SHARED_EMPTY_SYSOBJ(sysfs_hw_obj);        /* sysfs path: /hw */
DEF_SHARED_EMPTY_SYSOBJ(sysfs_power_obj);     /* sysfs path: /hw/power */
DEF_SHARED_EMPTY_SYSOBJ(sysfs_storage_obj);   /* sysfs path: /hw/storage */
DEF_SHARED_EMPTY_SYSOBJ(sysfs_network_obj);   /* sysfs path: /hw/network */
DEF_SHARED_EMPTY_SYSOBJ(sysfs_display_obj);   /* sysfs path: /hw/display */
DEF_SHARED_EMPTY_SYSOBJ(sysfs_media_obj);     /* sysfs path: /hw/media */
DEF_SHARED_EMPTY_SYSOBJ(sysfs_bridge_obj);    /* sysfs path: /hw/bridge */
DEF_SHARED_EMPTY_SYSOBJ(sysfs_comm_obj);      /* sysfs path: /hw/comm */
DEF_SHARED_EMPTY_SYSOBJ(sysfs_genp_obj);      /* sysfs path: /hw/generic */
DEF_SHARED_EMPTY_SYSOBJ(sysfs_input_obj);     /* sysfs path: /hw/input */
DEF_SHARED_EMPTY_SYSOBJ(sysfs_serbus_obj);    /* sysfs path: /hw/serbus */
DEF_SHARED_EMPTY_SYSOBJ(sysfs_wifi_obj);      /* sysfs path: /hw/wireless */
DEF_SHARED_EMPTY_SYSOBJ(sysfs_sigproc_obj);   /* sysfs path: /hw/sigproc */
DEF_SHARED_EMPTY_SYSOBJ(sysfs_other_obj);     /* sysfs path: /hw/other */

#define REGISTER_SYSFS_HW_OBJ(name, obj)  \
   sysfs_register_obj(sysfs, &sysfs_hw_obj, name, obj)

static int
main_sysfs_create_default_objects(void)
{
   int rc;

   if ((rc = sysfs_register_obj(sysfs, NULL, NULL, &sysfs_root_obj)))
      return rc;

   if ((rc = sysfs_register_obj(sysfs, &sysfs_root_obj, "hw", &sysfs_hw_obj)))
      return rc;

   if ((rc = REGISTER_SYSFS_HW_OBJ("power", &sysfs_power_obj)))
      return rc;
   if ((rc = REGISTER_SYSFS_HW_OBJ("storage", &sysfs_storage_obj)))
      return rc;
   if ((rc = REGISTER_SYSFS_HW_OBJ("network", &sysfs_network_obj)))
      return rc;
   if ((rc = REGISTER_SYSFS_HW_OBJ("display", &sysfs_display_obj)))
      return rc;
   if ((rc = REGISTER_SYSFS_HW_OBJ("media", &sysfs_media_obj)))
      return rc;
   if ((rc = REGISTER_SYSFS_HW_OBJ("bridge", &sysfs_bridge_obj)))
      return rc;
   if ((rc = REGISTER_SYSFS_HW_OBJ("comm", &sysfs_comm_obj)))
      return rc;
   if ((rc = REGISTER_SYSFS_HW_OBJ("generic", &sysfs_genp_obj)))
      return rc;
   if ((rc = REGISTER_SYSFS_HW_OBJ("input", &sysfs_input_obj)))
      return rc;
   if ((rc = REGISTER_SYSFS_HW_OBJ("serbus", &sysfs_serbus_obj)))
      return rc;
   if ((rc = REGISTER_SYSFS_HW_OBJ("wireless", &sysfs_wifi_obj)))
      return rc;
   if ((rc = REGISTER_SYSFS_HW_OBJ("sigproc", &sysfs_sigproc_obj)))
      return rc;
   if ((rc = REGISTER_SYSFS_HW_OBJ("other", &sysfs_other_obj)))
      return rc;

   return 0;
}


void
init_sysfs(void)
{
   int rc;

   if ((rc = vfs_mkdir("/syst", 0777)))
      panic("vfs_mkdir(\"/syst\") failed with error: %d", rc);

   sysfs = create_sysfs();

   if (!sysfs)
      panic("Unable to create sysfs");

   if ((rc = mp_add(sysfs, "/syst/")))
      panic("mp_add() failed with error: %d", rc);

   if (main_sysfs_create_default_objects() < 0)
      panic("Unable to create default objects");

   sysfs_create_config_obj();
}

static struct module sysfs_module = {

   .name = "sysfs",
   .priority = MOD_sysfs_prio,
   .init = &init_sysfs,
};

REGISTER_MODULE(&sysfs_module);
