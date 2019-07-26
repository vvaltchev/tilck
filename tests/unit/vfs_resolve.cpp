/* SPDX-License-Identifier: BSD-2-Clause */

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cstdio>
#include <cstring>

#include <iostream>
#include <vector>
#include <random>
#include <map>
#include <unordered_map>
#include <algorithm>

using namespace std;

#include <gtest/gtest.h>

#include "kernel_init_funcs.h"

extern "C" {

   #include <tilck/kernel/fs/vfs.h>
   #include <tilck/kernel/sched.h>
   #include <tilck/kernel/process.h>
   #include "kernel/fs/fs_int.h"

   void mountpoint_reset(void);
}


struct test_fs_elem {

   const char *name;
   test_fs_elem *parent;
   int ref_count;
   map<string, test_fs_elem *> c; /* children */
};

#define NODE_RAW(name, ...) new test_fs_elem{name, 0, 0, { __VA_ARGS__ }}
#define NODE(name, ...) make_pair(name, NODE_RAW( name, __VA_ARGS__ ))
#define ROOT_NODE(...) NODE_RAW("", __VA_ARGS__)

static test_fs_elem *fs1_root =
   ROOT_NODE(
      NODE(
         "a",
         NODE(
            "b",
            NODE(
               "c",
               NODE("f1"),
               NODE("f2"),
            ),
            NODE("c2"),
            NODE(".hdir"),
         )
      ),
      NODE("dev")
   );

static test_fs_elem *fs2_root =
   ROOT_NODE(
      NODE(
         "x",
         NODE(
            "y",
            NODE("z")
         )
      ),
      NODE("f_fs2_1"),
      NODE("f_fs2_2")
   );

static test_fs_elem *fs3_root =
   ROOT_NODE(
      NODE(
         "xd",
         NODE(
            "yd",
            NODE("zd")
         )
      ),
      NODE("fd1"),
      NODE("fd2")
   );

static bool are_test_fs_parents_set;

static void test_fs_do_set_parents(test_fs_elem *node)
{
   for (auto &p : node->c) {
      p.second->parent = node;
      test_fs_do_set_parents(p.second);
   }
}

void
testfs_get_entry(filesystem *fs,
                  void *dir_inode,
                  const char *name,
                  ssize_t name_len,
                  fs_path_struct *fs_path)
{
   if (!are_test_fs_parents_set) {
      test_fs_do_set_parents(fs1_root);
      test_fs_do_set_parents(fs2_root);
      test_fs_do_set_parents(fs3_root);
      are_test_fs_parents_set = true;
   }

   if (!name) {
      fs_path->type = VFS_DIR;
      fs_path->inode = fs->device_data;
      fs_path->dir_inode = fs->device_data;
      fs_path->dir_entry = nullptr;
      return;
   }

   string s{name, (size_t)name_len};
   test_fs_elem *e = (test_fs_elem *)dir_inode;
   //cout << "get_entry: '" << s << "'" << endl;

   if (s == "." || s == "..") {

      if (s == "..")
         e = e->parent;

      fs_path->inode = (void *)e;
      fs_path->type = e->name[0] == 'f' ? VFS_FILE : VFS_DIR;
      fs_path->dir_inode = e->parent;
      return;
   }


   auto it = e->c.find(s);

   if (it != e->c.end()) {
      fs_path->inode = it->second;
      fs_path->type = it->first[0] == 'f' ? VFS_FILE : VFS_DIR;
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
   test_fs_elem *e = (test_fs_elem *)i;
   return ++e->ref_count;
}

static int vfs_test_release_inode(filesystem *fs, vfs_inode_ptr_t i)
{
   test_fs_elem *e = (test_fs_elem *)i;
   assert(e->ref_count > 0);
   return --e->ref_count;
}

/*
 * Unfortunately, in C++ non-trivial designated initializers are not supported,
 * so we have to explicitly initialize all the members, in order!
 */
static const fs_ops static_fsops_testfs = {

   testfs_get_entry,             /* get_entry */
   NULL,                         /* get_inode */
   NULL,                         /* open */
   NULL,                         /* close */
   NULL,                         /* dup */
   NULL,                         /* getdents */
   NULL,                         /* unlink */
   NULL,                         /* stat */
   NULL,                         /* mkdir */
   NULL,                         /* rmdir */
   NULL,                         /* symlink */
   NULL,                         /* readlink */
   NULL,                         /* truncate */
   vfs_test_retain_inode,        /* retain_inode */
   vfs_test_release_inode,       /* release_inode */
   vfs_test_fs_exlock,           /* fs_exlock */
   vfs_test_fs_exunlock,         /* fs_exunlock */
   vfs_test_fs_shlock,           /* fs_shlock */
   vfs_test_fs_shunlock,         /* fs_shunlock */
};

static filesystem testfs1 = {

   1,                        /* ref-count */
   "testfs1",                /* fs type name */
   0,                        /* device_id */
   0,                        /* flags */
   fs1_root,                 /* device_data */
   &static_fsops_testfs,     /* fsops */
};

static filesystem testfs2 = {

   1,                        /* ref-count */
   "testfs2",                /* fs type name */
   0,                        /* device_id */
   0,                        /* flags */
   fs2_root,                 /* device_data */
   &static_fsops_testfs,     /* fsops */
};

static filesystem testfs3 = {

   1,                        /* ref-count */
   "testfs3",                /* fs type name */
   0,                        /* device_id */
   0,                        /* flags */
   fs3_root,                 /* device_data */
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

class vfs_resolve_test : public ::testing::Test {

protected:

   void SetUp() override {
      init_kmalloc_for_tests();
      create_kernel_process();
      mountpoint_reset();

      mountpoint_add(&testfs1, "/"); // older interface. TODO: remove
      mp2_init(&testfs1);
      mp2_add(&testfs2, "/a/b/c2");
      mp2_add(&testfs3, "/dev");
   }

   void TearDown() override {
      /* do nothing, at the moment */
   }
};

class vfs_resolve_multi_fs : public vfs_resolve_test { };

TEST_F(vfs_resolve_test, basic_test)
{
   int rc;
   vfs_path p;

   /* root path */
   rc = resolve("/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.dir_inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");

   /* regular 1-level path */
   rc = resolve("/a", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]);
   ASSERT_TRUE(p.fs_path.dir_inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "a");

   /* non-existent 1-level path */
   rc = resolve("/x", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == nullptr);
   ASSERT_TRUE(p.fs_path.dir_inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_NONE);
   ASSERT_STREQ(p.last_comp, "x");

   /* regular 2-level path */
   rc = resolve("/a/b", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "b");

   /* regular 2-level path + trailing slash */
   rc = resolve("/a/b/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "b/");

   /* 2-level path with non-existent component in the middle */
   rc = resolve("/x/b", &p, true);
   ASSERT_EQ(rc, -ENOENT);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);

   /* 4-level path ending with file */
   rc = resolve("/a/b/c/f1", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]->c["c"]->c["f1"]);
   ASSERT_TRUE(p.fs_path.dir_inode == fs1_root->c["a"]->c["b"]->c["c"]);
   ASSERT_TRUE(p.fs_path.type == VFS_FILE);
   ASSERT_STREQ(p.last_comp, "f1");

   /* 4-level path ending with file + trailing slash */
   rc = resolve("/a/b/c/f1/", &p, true);
   ASSERT_EQ(rc, -ENOTDIR);
   ASSERT_STREQ(p.last_comp, "f1/");
}

TEST_F(vfs_resolve_test, corner_cases)
{
   int rc;
   vfs_path p;

   /* empty path */
   rc = resolve("", &p, true);
   ASSERT_EQ(rc, -ENOENT);
   ASSERT_STREQ(p.last_comp, "");

   /* multiple slashes [root] */
   rc = resolve("/////", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");

   /* multiple slashes [in the middle] */
   rc = resolve("/a/b/c////f1", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]->c["c"]->c["f1"]);
   ASSERT_TRUE(p.fs_path.dir_inode == fs1_root->c["a"]->c["b"]->c["c"]);
   ASSERT_TRUE(p.fs_path.type == VFS_FILE);
   ASSERT_STREQ(p.last_comp, "f1");

   /* multiple slashes [at the beginning] */
   rc = resolve("//a/b/c/f1", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]->c["c"]->c["f1"]);
   ASSERT_TRUE(p.fs_path.type == VFS_FILE);
   ASSERT_STREQ(p.last_comp, "f1");

   /* multiple slashes [at the end] */
   rc = resolve("/a/b/////", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "b/////");

   /* dir entry starting with '.' */
   rc = resolve("/a/b/.hdir", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]->c[".hdir"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_TRUE(p.fs_path.dir_inode == fs1_root->c["a"]->c["b"]);
   ASSERT_STREQ(p.last_comp, ".hdir");

   /* dir entry starting with '.' + trailing slash */
   rc = resolve("/a/b/.hdir/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]->c[".hdir"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_TRUE(p.fs_path.dir_inode == fs1_root->c["a"]->c["b"]);
   ASSERT_STREQ(p.last_comp, ".hdir/");
}

TEST_F(vfs_resolve_test, single_dot)
{
   int rc;
   vfs_path p;

   rc = resolve("/a/.", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "a/.");

   rc = resolve("/a/./", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "a/./");

   rc = resolve("/.", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");

   rc = resolve("/./", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");

   rc = resolve("/a/./b/c", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]->c["c"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "c");
}

TEST_F(vfs_resolve_test, double_dot)
{
   int rc;
   vfs_path p;

   rc = resolve("/a/b/c/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");

   rc = resolve("/a/b/c/../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");

   rc = resolve("/a/b/c/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");

   rc = resolve("/a/b/c/../../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");

   rc = resolve("/a/b/c/../../new", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == nullptr);
   ASSERT_TRUE(p.fs_path.dir_inode == fs1_root->c["a"]);
   ASSERT_TRUE(p.fs_path.type == VFS_NONE);
   ASSERT_STREQ(p.last_comp, "new");

   rc = resolve("/a/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");

   rc = resolve("/a/../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "");

   rc = resolve("/a/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   //ASSERT_STREQ(p.last_comp, ".."); // TODO: fix!!

   rc = resolve("/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   //ASSERT_STREQ(p.last_comp, "..");

   rc = resolve("/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   //ASSERT_STREQ(p.last_comp, "..");
}

TEST_F(vfs_resolve_multi_fs, basic_case)
{
   int rc;
   vfs_path p;

   /* target-fs's root without slash */
   rc = resolve("/a/b/c2", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root);
   ASSERT_STREQ(p.last_comp, "c2");

   /* target-fs's root with slash */
   rc = resolve("/a/b/c2/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root);
   ASSERT_STREQ(p.last_comp, "c2/");

   rc = resolve("/a/b/c2/x", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root->c["x"]);
   ASSERT_STREQ(p.last_comp, "x");

   rc = resolve("/a/b/c2/f_fs2_1", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root->c["f_fs2_1"]);
   ASSERT_STREQ(p.last_comp, "f_fs2_1");

   rc = resolve("/a/b/c2/x/y", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root->c["x"]->c["y"]);
   ASSERT_STREQ(p.last_comp, "y");

   rc = resolve("/a/b/c2/x/y/z", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root->c["x"]->c["y"]->c["z"]);
   ASSERT_STREQ(p.last_comp, "z");

   rc = resolve("/a/b/c2/x/y/z/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root->c["x"]->c["y"]->c["z"]);
   ASSERT_STREQ(p.last_comp, "z/");
}

TEST_F(vfs_resolve_multi_fs, dot_dot)
{
   int rc;
   vfs_path p;

   rc = resolve("/a/b/c2/x/y/z/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root->c["x"]->c["y"]);
   ASSERT_STREQ(p.last_comp, "");

   rc = resolve("/a/b/c2/x/y/z/../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root->c["x"]->c["y"]);
   ASSERT_STREQ(p.last_comp, "");

   /* new file after '..' */
   rc = resolve("/a/b/c2/x/y/z/../new_file", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == NULL);
   ASSERT_TRUE(p.fs_path.dir_inode == fs2_root->c["x"]->c["y"]);
   ASSERT_STREQ(p.last_comp, "new_file");

   /* new dir after '..' */
   rc = resolve("/a/b/c2/x/y/z/../new_dir/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == NULL);
   ASSERT_TRUE(p.fs_path.dir_inode == fs2_root->c["x"]->c["y"]);
   ASSERT_STREQ(p.last_comp, "new_dir/");

   rc = resolve("/a/b/c2/x/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root);
   ASSERT_STREQ(p.last_comp, "");

   rc = resolve("/a/b/c2/x/../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root);
   ASSERT_TRUE(p.fs == &testfs2);
   ASSERT_STREQ(p.last_comp, "");

   /* ../ crossing the fs-boundary [c2 is a mount-point] */
   rc = resolve("/a/b/c2/x/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]);
   ASSERT_TRUE(p.fs == &testfs1);
   ASSERT_STREQ(p.last_comp, "");

   rc = resolve("/dev/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs == &testfs1);
   ASSERT_STREQ(p.last_comp, "");

   rc = resolve("/dev/../a", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]);
   ASSERT_TRUE(p.fs == &testfs1);
}

TEST_F(vfs_resolve_multi_fs, rel_paths)
{
   int rc;
   vfs_path p;
   process_info *pi = get_curr_task()->pi;

   rc = resolve("/dev/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs3_root);
   ASSERT_TRUE(p.fs == &testfs3);
   ASSERT_STREQ(p.last_comp, "dev/");

   pi->cwd2 = p;
   bzero(&p, sizeof(p));

   rc = resolve(".", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs3_root);
   ASSERT_TRUE(p.fs == &testfs3);
   ASSERT_STREQ(p.last_comp, "");

   rc = resolve("..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs == &testfs1);
   ASSERT_STREQ(p.last_comp, "");
}
