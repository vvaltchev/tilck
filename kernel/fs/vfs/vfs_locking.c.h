/* SPDX-License-Identifier: BSD-2-Clause */

void vfs_exlock(fs_handle h)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

   fs_handle_base *hb = (fs_handle_base *) h;

   if (hb->fops->exlock)
      hb->fops->exlock(h);
}

void vfs_exunlock(fs_handle h)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

   fs_handle_base *hb = (fs_handle_base *) h;

   if (hb->fops->exunlock)
      hb->fops->exunlock(h);
}

void vfs_shlock(fs_handle h)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

   fs_handle_base *hb = (fs_handle_base *) h;

   if (hb->fops->shlock)
      hb->fops->shlock(h);
}

void vfs_shunlock(fs_handle h)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(h != NULL);

   fs_handle_base *hb = (fs_handle_base *) h;

   if (hb->fops->shunlock)
      hb->fops->shunlock(h);
}

void vfs_fs_exlock(struct fs *fs)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(fs != NULL);
   ASSERT(fs->fsops->fs_exlock);

   fs->fsops->fs_exlock(fs);
}

void vfs_fs_exunlock(struct fs *fs)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(fs != NULL);
   ASSERT(fs->fsops->fs_exunlock);

   fs->fsops->fs_exunlock(fs);
}

void vfs_fs_shlock(struct fs *fs)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(fs != NULL);
   ASSERT(fs->fsops->fs_shlock);

   fs->fsops->fs_shlock(fs);
}

void vfs_fs_shunlock(struct fs *fs)
{
   NO_TEST_ASSERT(is_preemption_enabled());
   ASSERT(fs != NULL);
   ASSERT(fs->fsops->fs_shunlock);

   fs->fsops->fs_shunlock(fs);
}
