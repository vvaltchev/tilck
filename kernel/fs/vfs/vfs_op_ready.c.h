/* SPDX-License-Identifier: BSD-2-Clause */

bool vfs_read_ready(fs_handle h)
{
   struct fs_handle_base *hb = (struct fs_handle_base *) h;
   bool r;

   if (!hb->fops->read_ready)
      return true;

   vfs_shlock(h);
   {
      r = hb->fops->read_ready(h);
   }
   vfs_shunlock(h);
   return r;
}

bool vfs_write_ready(fs_handle h)
{
   struct fs_handle_base *hb = (struct fs_handle_base *) h;
   bool r;

   if (!hb->fops->write_ready)
      return true;

   vfs_shlock(h);
   {
      r = hb->fops->write_ready(h);
   }
   vfs_shunlock(h);
   return r;
}

bool vfs_except_ready(fs_handle h)
{
   struct fs_handle_base *hb = (struct fs_handle_base *) h;
   bool r;

   if (!hb->fops->except_ready)
      return false;

   vfs_shlock(h);
   {
      r = hb->fops->except_ready(h);
   }
   vfs_shunlock(h);
   return r;
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
