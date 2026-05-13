/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/process_mm.h>
#include "sysfs_int.h"

static offt
sysfs_call_load(struct sysobj *obj,
                struct sysobj_prop *prop,
                void *prop_data,
                void *load_buf,
                offt buf_len,
                offt off)
{
   offt rc;

   if (obj->hooks && obj->hooks->pre_load) {

      rc = obj->hooks->pre_load(obj, prop, prop_data);

      if (rc < 0)
         return rc;
   }

   rc = prop->type->load(obj, prop_data, load_buf, buf_len, off);

   if (obj->hooks && obj->hooks->post_load)
      obj->hooks->post_load(obj, prop, prop_data);

   return rc;
}

static offt
sysfs_call_store(struct sysobj *obj,
                 struct sysobj_prop *prop,
                 void *prop_data,
                 void *buf,
                 offt buf_len)
{
   offt rc;

   if (obj->hooks && obj->hooks->pre_store) {

      rc = obj->hooks->pre_store(obj, prop, prop_data);

      if (rc < 0)
         return rc;
   }

   rc = prop->type->store(obj, prop_data, buf, buf_len);

   if (obj->hooks && obj->hooks->post_store)
      obj->hooks->post_store(obj, prop, prop_data);

   return rc;
}

static int
sysfs_load_data(struct sysfs_handle *h)
{
   struct sysfs_inode *i = h->inode;
   struct sysobj_prop *prop = i->file.prop;
   struct sysobj *obj = i->file.obj;
   void *prop_data = i->file.prop_data;
   void *data;
   offt rc;

   ASSERT(!h->file.data);
   ASSERT(!h->file.data_len);
   ASSERT(h->file.data_max_len > 0);
   ASSERT(prop->type);

   data = kmalloc((size_t)h->file.data_max_len);

   if (!data)
      return -ENOMEM;

   rc = sysfs_call_load(obj, prop, prop_data, data, h->file.data_max_len, 0);

   if (rc < 0) {
      kfree2(data, (size_t)h->file.data_max_len);
      return (int)rc;
   }

   disable_preemption();
   {
      h->file.data = data;
      h->file.data_len = rc;
   }
   enable_preemption();
   return 0;
}

static void
sysfs_flush_data(struct sysfs_handle *sh)
{
   ASSERT(sh->type == VFS_FILE);

   struct sysfs_inode *i = sh->inode;
   struct sysobj_prop *prop = i->file.prop;
   struct sysobj *obj = i->file.obj;
   void *prop_data = i->file.prop_data;
   offt data_len = sh->file.data_len;
   void *data = sh->file.data;

   ASSERT(prop->type);
   ASSERT(sh->file.data_max_len > 0);
   ASSERT(sh->file.data != NULL);

   disable_preemption();
   {
      if (list_is_node_in_list(&sh->file.dirty_node)) {

         /* We have to flush our buffer with a store() operation */

         if (sysfs_call_store(obj, prop, prop_data, data, data_len) >= 0) {

            list_remove(&sh->file.dirty_node);
            list_node_init(&sh->file.dirty_node);

         } else {

            /* We ignore a failure in this case */
         }
      }
   }
   enable_preemption();
}

static void
sysfs_free_data(struct sysfs_handle *sh)
{
   /* Reset the data* fields the way sysfs_load_data() expects them */
   ASSERT(!list_is_node_in_list(&sh->file.dirty_node));

   if (sh->file.data) {
      kfree2(sh->file.data, (size_t)sh->file.data_max_len);
   }

   sh->file.data = NULL;
   sh->file.data_len = 0;
}

static void
sysfs_flush_and_free_data(struct sysfs_handle *sh)
{
   sysfs_flush_data(sh);
   sysfs_free_data(sh);
}

static int
sysfs_fsync(fs_handle h)
{
   struct sysfs_handle *sh = h;
   struct sysobj_prop *prop = sh->inode->file.prop;

   if (prop->type && prop->type->buf_type == SYSFS_BUF_BUFFERED) {

      if (sh->file.data)
         sysfs_flush_data(sh);
   }

   return 0;
}

static ssize_t
sysfs_file_read(fs_handle h, char *buf, size_t len, offt *pos)
{
   struct sysfs_handle *sh = h;
   struct sysfs_inode *i = sh->inode;
   struct sysobj_prop *prop = i->file.prop;
   struct sysobj *obj = i->file.obj;
   void *pd = i->file.prop_data;
   offt rc = 0, rem;

   if (!prop->type || !prop->type->load)
      return 0;

   switch (prop->type->buf_type) {

      case SYSFS_BUF_ONESHOT:

         if (*pos == 0) {
            rc = sysfs_call_load(obj, prop, pd, buf, (offt)len, 0);
            *pos = LONG_MAX;
         }
         break;

      case SYSFS_BUF_IMMUTABLE:

         rc = sysfs_call_load(obj, prop, pd, buf, (offt)len, *pos);

         if (rc > 0)
            *pos += rc;
         break;

      case SYSFS_BUF_BUFFERED:

         if (!sh->file.data) {
            if (sysfs_load_data(sh))
               return 0;   /* no memory for the per-handle buffer */
         }

         rem = sh->file.data_len - *pos;
         ASSERT(rem >= 0);

         rc = CLAMP(rem, 0, (offt)len);
         memcpy(buf, sh->file.data + *pos, (size_t)rc);
         *pos += rc;
         break;

      case SYSFS_BUF_STREAM:

         /*
          * Stream mode: call load() afresh on every read(), don't
          * advance *pos, never report EOF. The load() implementation
          * may block.
          */
         rc = sysfs_call_load(obj, prop, pd, buf, (offt)len, 0);
         break;
   }

   return (ssize_t)rc;
}

static ssize_t
sysfs_file_write(fs_handle h, char *buf, size_t len, offt *pos)
{
   struct sysfs_handle *sh = h;
   struct sysfs_inode *i = sh->inode;
   struct sysobj_prop *prop = i->file.prop;
   struct sysobj *obj = i->file.obj;
   void *pd = i->file.prop_data;
   struct sysfs_data *d = sh->fs->device_data;
   offt rc, rem;

   if (!prop->type || !prop->type->store)
      return -EINVAL;

   switch (prop->type->buf_type) {

      case SYSFS_BUF_ONESHOT:

         rc = sysfs_call_store(obj, prop, pd, buf, (offt)len);
         break;

      case SYSFS_BUF_IMMUTABLE:

         /*
          * Immutable types should not define a store callback at all,
          * but if one slips through (e.g. a shared prop_type), we
          * explicitly reject the write here for the symmetry with the
          * read path.
          */
         rc = -EINVAL;
         break;

      case SYSFS_BUF_BUFFERED:

         if (!sh->file.data) {
            if (sysfs_load_data(sh))
               return -ENOSPC;   /* no memory for the per-handle buffer */
         }

         rem = sh->file.data_max_len - *pos;
         ASSERT(rem >= 0);

         rc = CLAMP(rem, 0, (offt)len);
         memcpy(sh->file.data + *pos, buf, (size_t)rc);
         *pos += rc;

         disable_preemption();
         {
            if (*pos > sh->file.data_len)
               sh->file.data_len = *pos;

            list_add_tail(&d->dirty_handles, &sh->file.dirty_node);
         }
         enable_preemption();
         break;

      case SYSFS_BUF_STREAM:

         /* Stream mode: store() called once per write(), no buffering. */
         rc = sysfs_call_store(obj, prop, pd, buf, (offt)len);
         break;

      default:
         rc = -EINVAL;
         break;
   }

   return (ssize_t)rc;
}

static int
sysfs_file_ioctl(fs_handle h, ulong request, void *arg)
{
   return -EINVAL;
}

static offt
sysfs_file_seek(fs_handle h, offt target_off, int whence)
{
   struct sysfs_handle *sh = h;
   struct sysobj_prop *prop = sh->inode->file.prop;
   const offt len = sh->file.data_max_len;
   offt new_pos = sh->h_fpos;

   if (prop->type && prop->type->buf_type == SYSFS_BUF_STREAM)
      return -ESPIPE;

   switch (whence) {

      case SEEK_SET:
         new_pos = target_off;
         break;

      case SEEK_CUR:
         new_pos += target_off;
         break;

      case SEEK_END:
         new_pos = len + target_off;
         break;

      default:
         return -EINVAL;
   }

   if (new_pos < 0 || (!len && new_pos > 0))
      return -EINVAL;

   sh->h_fpos = new_pos;
   return new_pos;
}

static void
sysfs_on_close(fs_handle h)
{
   struct sysfs_handle *sh = h;
   struct sysobj_prop *prop;

   if (sh->type != VFS_FILE)
      return;

   prop = sh->inode->file.prop;

   if (prop->type && prop->type->buf_type == SYSFS_BUF_BUFFERED &&
       sh->file.data)
   {
      sysfs_flush_and_free_data(sh);
   }
}

static int
sysfs_on_dup(fs_handle new_h)
{
   struct sysfs_handle *h2 = new_h;

   if (h2->type != VFS_FILE)
      return 0;

   if (!h2->inode->file.prop->type)
      return 0;

   if (h2->inode->file.prop->type->buf_type == SYSFS_BUF_BUFFERED) {

      if (h2->file.data) {

         /*
          * Flush the data in the buffers using the old handle in order to
          * guarantee up-to-date data in case userland code decides to write
          * on the file using the new handle.
          */
         sysfs_flush_data(h2);

         /*
          * Reset the data field in the new handle: it will be reloaded on
          * demand in case of read/write.
          */
         h2->file.data = NULL;
         sysfs_free_data(h2);       /* performs just reset */
      }
   }

   return 0;
}

int sysfs_mmap(struct user_mapping *um, pdir_t *pdir, int flags)
{
   struct sysfs_handle *sh = um->h;
   struct sysfs_inode *inode = sh->inode;
   struct sysobj_prop *prop = inode->file.prop;
   const struct sysobj_prop_type *pt = prop->type;
   const size_t pg_count = um->len >> PAGE_SHIFT;
   ulong vaddr = um->vaddr;
   size_t mapped_cnt, buf_sz;
   void *data = NULL;

   if (sh->type != VFS_FILE)
      return -EACCES;

   if (flags & VFS_MM_DONT_MMAP)
      return 0;

   if (!pt || pt->buf_type != SYSFS_BUF_IMMUTABLE)
      return -EACCES; /* mmap only supported on immutable backing data */

   buf_sz = (size_t)sh->file.data_max_len;

   if (!pt->get_data_ptr)
      return -EACCES;

   data = pt->get_data_ptr(inode->file.obj, inode->file.prop_data);

   if (!data)
      return -EACCES;

   if (um->off + um->len > buf_sz)
      return -EACCES;

   mapped_cnt = map_pages(pdir,
                          (void *)vaddr,
                          LIN_VA_TO_PA(data) + um->off,
                          pg_count,
                          PAGING_FL_US | PAGING_FL_SHARED);

   if (mapped_cnt != pg_count) {
      unmap_pages_permissive(pdir, (void *)vaddr, mapped_cnt, false);
      return -ENOMEM;
   }

   return 0;
}

int sysfs_munmap(struct user_mapping *um, void *vaddrp, size_t len)
{
   struct sysfs_handle *sh = um->h;
   struct sysobj_prop *prop = sh->inode->file.prop;

   if (sh->type != VFS_FILE)
      return -EACCES;

   if (!prop->type || prop->type->buf_type != SYSFS_BUF_IMMUTABLE)
      return -EACCES;

   return generic_fs_munmap(um, vaddrp, len);
}

static const struct file_ops static_ops_file_sysfs =
{
   .read = sysfs_file_read,
   .write = sysfs_file_write,
   .seek = sysfs_file_seek,
   .ioctl = sysfs_file_ioctl,
   .mmap = sysfs_mmap,
   .munmap = sysfs_munmap,
   .sync = sysfs_fsync,
   .datasync = sysfs_fsync,
};

static int
sysfs_open_file(struct mnt_fs *fs, struct sysfs_inode *pos, fs_handle *out)
{
   struct sysfs_handle *h;
   struct sysobj_prop *prop = pos->file.prop;
   void *prop_data = pos->file.prop_data;
   offt buf_sz = 0, rc;
   enum sysfs_buf_type bt = SYSFS_BUF_ONESHOT;

   if (prop->type) {

      bt = prop->type->buf_type;

      if (bt == SYSFS_BUF_BUFFERED || bt == SYSFS_BUF_IMMUTABLE) {

         ASSERT(prop->type->get_buf_sz != NULL);
         buf_sz = prop->type->get_buf_sz(pos->file.obj, prop_data);
         ASSERT(buf_sz > 0);
      }
   }

   if (!(h = vfs_create_new_handle(fs, &static_ops_file_sysfs)))
      return -ENOMEM;

   h->type = VFS_FILE;
   h->file.data = NULL;
   h->file.data_len = 0;
   h->file.data_max_len = buf_sz;
   h->spec_flags = VFS_SPFL_NO_LF;
   h->inode = pos;
   list_node_init(&h->file.dirty_node);
   retain_obj(pos);

   if (bt == SYSFS_BUF_BUFFERED) {
      if ((rc = sysfs_load_data(h))) {
         vfs_close(h);
         return (int)rc;
      }
   }

   *out = h;
   return 0;
}
