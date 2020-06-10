/* SPDX-License-Identifier: BSD-2-Clause */

#include "vfs_test_fs.h"

static map<tfs_entry *, tfs_entry *> test_mps;

void test_fs_register_mp(tfs_entry *where, tfs_entry *target)
{
   auto it = test_mps.find(where);
   ASSERT_TRUE(it == test_mps.end());

   test_mps[where] = target;
}

void test_fs_clear_mps()
{
   test_mps.clear();
}

bool test_fs_is_mountpoint(tfs_entry *e)
{
   return test_mps.find(e) != test_mps.end();
}

void test_fs_check_refcounts(tfs_entry *node)
{
   for (auto &p : node->children) {

      tfs_entry *e = p.second;

      if (test_fs_is_mountpoint(e)) {
         ASSERT_EQ(e->ref_count, 1) << "[Info] mp node: " << p.first;
      } else {
         ASSERT_EQ(e->ref_count, 0) << "[Info] node: " << p.first;
      }

      test_fs_check_refcounts(e);
   }
}

static void
testfs_get_entry(struct fs *fs,
                 void *dir_inode,
                 const char *name,
                 ssize_t name_len,
                 struct fs_path *fs_path)
{
   if (!dir_inode && !name) {
      fs_path->type = VFS_DIR;
      fs_path->inode = fs->device_data;
      fs_path->dir_inode = fs->device_data;
      fs_path->dir_entry = nullptr;
      return;
   }

   string s{name, (size_t)name_len};
   tfs_entry *e = (tfs_entry *)dir_inode;
   //cout << "get_entry: '" << s << "'" << endl;

   if (s == "." || s == "..") {

      if (s == ".." && e->parent != nullptr)
         e = e->parent;

      fs_path->inode = (void *)e;
      fs_path->type = e->type;
      fs_path->dir_inode = e->parent;
      return;
   }


   auto it = e->children.find(s);

   if (it != e->children.end()) {
      fs_path->inode = it->second;
      fs_path->type = it->second->type;
   } else {
      fs_path->inode = nullptr;
      fs_path->type = VFS_NONE;
   }

   fs_path->dir_inode = e;
}

static void vfs_test_fs_exlock(struct fs *fs)
{
   //printf("EXLOCK: %s\n", fs->fs_type_name);
}

static void vfs_test_fs_exunlock(struct fs *fs)
{
   //printf("EXUNLOCK: %s\n", fs->fs_type_name);
}

static void vfs_test_fs_shlock(struct fs *fs)
{
   //printf("SHLOCK: %s\n", fs->fs_type_name);
}

static void vfs_test_fs_shunlock(struct fs *fs)
{
   //printf("SHUNLOCK: %s\n", fs->fs_type_name);
}

static int vfs_test_retain_inode(struct fs *fs, vfs_inode_ptr_t i)
{
   tfs_entry *e = (tfs_entry *)i;
   return ++e->ref_count;
}

static int vfs_test_release_inode(struct fs *fs, vfs_inode_ptr_t i)
{
   tfs_entry *e = (tfs_entry *)i;
   assert(e->ref_count > 0);
   return --e->ref_count;
}

static int test_fs_readlink(struct vfs_path *rp, char *buf)
{
   tfs_entry *e = (tfs_entry *)rp->fs_path.inode;

   if (e->type != VFS_SYMLINK)
      return -ENOLINK;

   strcpy(buf, e->symlink);
   return strlen(e->symlink);
}

/*
 * Unfortunately, in C++ non-trivial designated initializers are fully not
 * supported, so we have to explicitly initialize all the members, in order!
 */
extern const struct fs_ops static_fsops_testfs = {

   .get_entry           = testfs_get_entry,
   .get_inode           = nullptr,
   .open                = nullptr,
   .close               = nullptr,
   .dup                 = nullptr,
   .getdents            = nullptr,
   .unlink              = nullptr,
   .stat                = nullptr,
   .mkdir               = nullptr,
   .rmdir               = nullptr,
   .symlink             = nullptr,
   .readlink            = test_fs_readlink,
   .truncate            = nullptr,
   .chmod               = nullptr,
   .rename              = nullptr,
   .link                = nullptr,
   .futimens            = nullptr,
   .retain_inode        = vfs_test_retain_inode,
   .release_inode       = vfs_test_release_inode,
   .fs_exlock           = vfs_test_fs_exlock,
   .fs_exunlock         = vfs_test_fs_exunlock,
   .fs_shlock           = vfs_test_fs_shlock,
   .fs_shunlock         = vfs_test_fs_shunlock,
   .exlock_noblk        = nullptr,
   .exunlock            = nullptr,
};

