/* SPDX-License-Identifier: BSD-2-Clause */

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
