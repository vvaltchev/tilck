/* SPDX-License-Identifier: BSD-2-Clause */

#include <iostream>
#include <map>
#include "vfs_test.h"
using namespace std;

struct tfs_entry {

   const char *name;
   vfs_entry_type type;
   const char *symlink;
   tfs_entry *parent;
   int ref_count;
   map<string, tfs_entry *> children;

   tfs_entry *set_parents() {

      for (auto &p : children) {
         p.second->parent = this;
         p.second->set_parents();
      }

      return this;
   }
};


#define _NODE(n, t, s, ...) new tfs_entry{n, t, s, 0, 0, {__VA_ARGS__}}
#define N_FILE(name) make_pair(name, _NODE(name, VFS_FILE, 0))
#define N_SYM(name, s) make_pair(name, _NODE(name, VFS_FILE, s))
#define N_DIR(name, ...) make_pair(name, _NODE(name, VFS_DIR, 0, __VA_ARGS__))
#define ROOT_NODE(...) (_NODE("", VFS_DIR, 0, __VA_ARGS__))->set_parents()

static tfs_entry *fs1 =
   ROOT_NODE(
      N_DIR(
         "a",
         N_DIR(
            "b",
            N_DIR(
               "c",
               N_FILE("f1"),
               N_FILE("f2"),
            ),
            N_DIR("c2"),
            N_DIR(".hdir"),
         )
      ),
      N_DIR("dev")
   );

static tfs_entry *fs2 =
   ROOT_NODE(
      N_DIR(
         "x",
         N_DIR(
            "y",
            N_DIR("z")
         )
      ),
      N_FILE("fs2_1"),
      N_FILE("fs2_2")
   );

static tfs_entry *fs3 =
   ROOT_NODE(
      N_DIR(
         "xd",
         N_DIR(
            "yd",
            N_DIR("zd")
         )
      ),
      N_FILE("fd1"),
      N_FILE("fd2")
   );

static map<tfs_entry *, tfs_entry *> test_mps;

static void test_fs_register_mp(tfs_entry *where, tfs_entry *target)
{
   auto it = test_mps.find(where);
   ASSERT_TRUE(it == test_mps.end());

   test_mps[where] = target;
}

static void test_fs_clear_mps()
{
   test_mps.clear();
}

static bool test_fs_is_mountpoint(tfs_entry *e)
{
   return test_mps.find(e) != test_mps.end();
}

static void test_fs_reset_refcounts(tfs_entry *node)
{
   for (auto &p : node->children) {
      p.second->ref_count = 0;
      test_fs_reset_refcounts(p.second);
   }
}

static void reset_all_fs_refcounts()
{
   test_fs_reset_refcounts(fs1);
   test_fs_reset_refcounts(fs2);
   test_fs_reset_refcounts(fs3);
}

static void test_fs_check_refcounts(tfs_entry *node)
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

static void check_all_fs_refcounts()
{
   test_fs_check_refcounts(fs1);
   test_fs_check_refcounts(fs2);
   test_fs_check_refcounts(fs3);
}

static void
testfs_get_entry(filesystem *fs,
                 void *dir_inode,
                 const char *name,
                 ssize_t name_len,
                 fs_path_struct *fs_path)
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

      if (s == "..")
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

static void vfs_test_fs_exlock(filesystem *fs)
{
   //printf("EXLOCK: %s\n", fs->fs_type_name);
}

static void vfs_test_fs_exunlock(filesystem *fs)
{
   //printf("EXUNLOCK: %s\n", fs->fs_type_name);
}

static void vfs_test_fs_shlock(filesystem *fs)
{
   //printf("SHLOCK: %s\n", fs->fs_type_name);
}

static void vfs_test_fs_shunlock(filesystem *fs)
{
   //printf("SHUNLOCK: %s\n", fs->fs_type_name);
}

static int vfs_test_retain_inode(filesystem *fs, vfs_inode_ptr_t i)
{
   tfs_entry *e = (tfs_entry *)i;
   return ++e->ref_count;
}

static int vfs_test_release_inode(filesystem *fs, vfs_inode_ptr_t i)
{
   tfs_entry *e = (tfs_entry *)i;
   assert(e->ref_count > 0);
   return --e->ref_count;
}

/*
 * Unfortunately, in C++ non-trivial designated initializers are fully not
 * supported, so we have to explicitly initialize all the members, in order!
 */
static const fs_ops static_fsops_testfs = {

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
   .readlink            = nullptr,
   .truncate            = nullptr,
   .retain_inode        = vfs_test_retain_inode,
   .release_inode       = vfs_test_release_inode,
   .fs_exlock           = vfs_test_fs_exlock,
   .fs_exunlock         = vfs_test_fs_exunlock,
   .fs_shlock           = vfs_test_fs_shlock,
   .fs_shunlock         = vfs_test_fs_shunlock,
};

static filesystem testfs1 = {

   1,                        /* ref-count */
   "testfs1",                /* fs type name */
   0,                        /* device_id */
   0,                        /* flags */
   fs1,                      /* device_data */
   &static_fsops_testfs,     /* fsops */
};

static filesystem testfs2 = {

   1,                        /* ref-count */
   "testfs2",                /* fs type name */
   0,                        /* device_id */
   0,                        /* flags */
   fs2,                      /* device_data */
   &static_fsops_testfs,     /* fsops */
};

static filesystem testfs3 = {

   1,                        /* ref-count */
   "testfs3",                /* fs type name */
   0,                        /* device_id */
   0,                        /* flags */
   fs3,                      /* device_data */
   &static_fsops_testfs,     /* fsops */
};


static int resolve(const char *path, vfs_path *p, bool res_last_sl)
{
   int rc;

   if ((rc = vfs_resolve(path, p, true, res_last_sl)) < 0)
      return rc;

   vfs_fs_exunlock(p->fs);
   release_obj(p->fs);
   return rc;
}

static tfs_entry *
path(tfs_entry *e, initializer_list<const char *> comps)
{
   for (auto comp_name : comps) {

      auto it = e->children.find(comp_name);

      if (it == e->children.end())
         return nullptr;

      e = it->second;
   }

   return e;
}


class vfs_resolve_test : public vfs_test_base {

protected:

   void SetUp() override {

      vfs_test_base::SetUp();

      mp2_init(&testfs1);
      mp2_add(&testfs2, "/a/b/c2");
      mp2_add(&testfs3, "/dev");

      test_fs_register_mp(path(fs1, {"a", "b", "c2"}), fs2);
      test_fs_register_mp(path(fs1, {"dev"}), fs3);
   }

   void TearDown() override {

      // TODO: call mp2_remove() for each fs
      reset_all_fs_refcounts();
      test_fs_clear_mps();
      vfs_test_base::TearDown();
   }
};

class vfs_resolve_multi_fs : public vfs_resolve_test { };
class vfs_resolve_symlinks : public vfs_resolve_test { };

TEST_F(vfs_resolve_test, basic_test)
{
   int rc;
   vfs_path p;

   /* root path */
   rc = resolve("/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1);
   ASSERT_TRUE(p.fs_path.dir_inode == fs1);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* regular 1-level path */
   rc = resolve("/a", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(fs1, {"a"}));
   ASSERT_TRUE(p.fs_path.dir_inode == fs1);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "a");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* non-existent 1-level path */
   rc = resolve("/x", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == nullptr);
   ASSERT_TRUE(p.fs_path.dir_inode == fs1);
   ASSERT_TRUE(p.fs_path.type == VFS_NONE);
   ASSERT_STREQ(p.last_comp, "x");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* regular 2-level path */
   rc = resolve("/a/b", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(fs1, {"a", "b"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "b");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* regular 2-level path + trailing slash */
   rc = resolve("/a/b/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(fs1, {"a", "b"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "b/");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* 2-level path with non-existent component in the middle */
   rc = resolve("/x/b", &p, true);
   ASSERT_EQ(rc, -ENOENT);
   ASSERT_TRUE(p.fs_path.inode == fs1);
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* 4-level path ending with file */
   rc = resolve("/a/b/c/f1", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(fs1, {"a", "b", "c", "f1"}));
   ASSERT_TRUE(p.fs_path.dir_inode == path(fs1, {"a", "b", "c"}));
   ASSERT_TRUE(p.fs_path.type == VFS_FILE);
   ASSERT_STREQ(p.last_comp, "f1");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* 4-level path ending with file + trailing slash */
   rc = resolve("/a/b/c/f1/", &p, true);
   ASSERT_EQ(rc, -ENOTDIR);
   ASSERT_STREQ(p.last_comp, "f1/");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });
}

TEST_F(vfs_resolve_test, corner_cases)
{
   int rc;
   vfs_path p;

   /* empty path */
   rc = resolve("", &p, true);
   ASSERT_EQ(rc, -ENOENT);
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* multiple slashes [root] */
   rc = resolve("/////", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* multiple slashes [in the middle] */
   rc = resolve("/a/b/c////f1", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(fs1, {"a", "b", "c", "f1"}));
   ASSERT_TRUE(p.fs_path.dir_inode == path(fs1, {"a", "b", "c"}));
   ASSERT_TRUE(p.fs_path.type == VFS_FILE);
   ASSERT_STREQ(p.last_comp, "f1");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* multiple slashes [at the beginning] */
   rc = resolve("//a/b/c/f1", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(fs1, {"a", "b", "c", "f1"}));
   ASSERT_TRUE(p.fs_path.type == VFS_FILE);
   ASSERT_STREQ(p.last_comp, "f1");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* multiple slashes [at the end] */
   rc = resolve("/a/b/////", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(fs1, {"a", "b"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "b/////");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* dir entry starting with '.' */
   rc = resolve("/a/b/.hdir", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(fs1, {"a", "b", ".hdir"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_TRUE(p.fs_path.dir_inode == path(fs1, {"a", "b"}));
   ASSERT_STREQ(p.last_comp, ".hdir");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* dir entry starting with '.' + trailing slash */
   rc = resolve("/a/b/.hdir/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(fs1, {"a", "b", ".hdir"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_TRUE(p.fs_path.dir_inode == path(fs1, {"a", "b"}));
   ASSERT_STREQ(p.last_comp, ".hdir/");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });
}

TEST_F(vfs_resolve_test, single_dot)
{
   int rc;
   vfs_path p;

   rc = resolve("/a/.", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(fs1, {"a"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "a/.");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/./", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(fs1, {"a"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "a/./");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/.", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/./", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/./b/c", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(fs1, {"a", "b", "c"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "c");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });
}

TEST_F(vfs_resolve_test, double_dot)
{
   int rc;
   vfs_path p;

   rc = resolve("/a/b/c/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(fs1, {"a", "b"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c/../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(fs1, {"a", "b"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(fs1, {"a"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c/../../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(fs1, {"a"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c/../../new", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == nullptr);
   ASSERT_TRUE(p.fs_path.dir_inode == path(fs1, {"a"}));
   ASSERT_TRUE(p.fs_path.type == VFS_NONE);
   ASSERT_STREQ(p.last_comp, "new");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });
}

TEST_F(vfs_resolve_multi_fs, basic_case)
{
   int rc;
   vfs_path p;

   /* target-fs's root without slash */
   rc = resolve("/a/b/c2", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == fs2);
   ASSERT_STREQ(p.last_comp, "c2");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* target-fs's root with slash */
   rc = resolve("/a/b/c2/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == fs2);
   ASSERT_STREQ(p.last_comp, "c2/");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c2/x", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == path(fs2, {"x"}));
   ASSERT_STREQ(p.last_comp, "x");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c2/fs2_1", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == path(fs2, {"fs2_1"}));
   ASSERT_STREQ(p.last_comp, "fs2_1");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c2/x/y", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == path(fs2, {"x", "y"}));
   ASSERT_STREQ(p.last_comp, "y");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c2/x/y/z", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == path(fs2, {"x", "y", "z"}));
   ASSERT_STREQ(p.last_comp, "z");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c2/x/y/z/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == path(fs2, {"x", "y", "z"}));
   ASSERT_STREQ(p.last_comp, "z/");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });
}

TEST_F(vfs_resolve_multi_fs, dot_dot)
{
   int rc;
   vfs_path p;

   rc = resolve("/a/b/c2/x/y/z/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == path(fs2, {"x", "y"}));
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c2/x/y/z/../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == path(fs2, {"x", "y"}));
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* new file after '..' */
   rc = resolve("/a/b/c2/x/y/z/../new_file", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == nullptr);
   ASSERT_TRUE(p.fs_path.dir_inode == path(fs2, {"x", "y"}));
   ASSERT_STREQ(p.last_comp, "new_file");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* new dir after '..' */
   rc = resolve("/a/b/c2/x/y/z/../new_dir/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == nullptr);
   ASSERT_TRUE(p.fs_path.dir_inode == path(fs2, {"x", "y"}));
   ASSERT_STREQ(p.last_comp, "new_dir/");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c2/x/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == fs2);
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c2/x/../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == fs2);
   ASSERT_TRUE(p.fs == &testfs2);
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* ../ crossing the fs-boundary [c2 is a mount-point] */
   rc = resolve("/a/b/c2/x/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == path(fs1, {"a", "b"}));
   ASSERT_TRUE(p.fs == &testfs1);
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/dev/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == fs1);
   ASSERT_TRUE(p.fs == &testfs1);
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/dev/../a", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == path(fs1, {"a"}));
   ASSERT_TRUE(p.fs == &testfs1);
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });
}

TEST_F(vfs_resolve_multi_fs, rel_paths)
{
   int rc;
   vfs_path p;
   process_info *pi = get_curr_task()->pi;

   rc = resolve("/dev/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == fs3);
   ASSERT_TRUE(p.fs == &testfs3);
   ASSERT_STREQ(p.last_comp, "dev/");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   pi->cwd2 = p;
   bzero(&p, sizeof(p));

   rc = resolve(".", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == fs3);
   ASSERT_TRUE(p.fs == &testfs3);
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == fs1);
   ASSERT_TRUE(p.fs == &testfs1);
   ASSERT_STREQ(p.last_comp, "");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });
}

TEST_F(vfs_resolve_symlinks, basic_tests)
{

}
