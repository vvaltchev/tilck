/* SPDX-License-Identifier: BSD-2-Clause */

bool vfs_read_ready(fs_handle h)
{
   struct fs_handle_base *hb = (struct fs_handle_base *) h;

   if (!hb->fops->read_ready)
      return true;

   return hb->fops->read_ready(h);
}

bool vfs_write_ready(fs_handle h)
{
   struct fs_handle_base *hb = (struct fs_handle_base *) h;

   if (!hb->fops->write_ready)
      return true;

   return hb->fops->write_ready(h);
}

bool vfs_except_ready(fs_handle h)
{
   struct fs_handle_base *hb = (struct fs_handle_base *) h;

   if (!hb->fops->except_ready)
      return false;

   return hb->fops->except_ready(h);
}

struct kcond *vfs_get_rready_cond(fs_handle h)
{
   struct fs_handle_base *hb = (struct fs_handle_base *) h;

   if (!hb->fops->get_rready_cond)
      return NULL;

   return hb->fops->get_rready_cond(h);
}

struct kcond *vfs_get_wready_cond(fs_handle h)
{
   struct fs_handle_base *hb = (struct fs_handle_base *) h;

   if (!hb->fops->get_wready_cond)
      return NULL;

   return hb->fops->get_wready_cond(h);
}

struct kcond *vfs_get_except_cond(fs_handle h)
{
   struct fs_handle_base *hb = (struct fs_handle_base *) h;

   if (!hb->fops->get_except_cond)
      return NULL;

   return hb->fops->get_except_cond(h);
}
