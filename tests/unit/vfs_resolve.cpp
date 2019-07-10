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

   void mountpoint_reset(void);

   int __vfs_resolve(const char *path,
                     vfs_path *rp,
                     bool exlock,
                     bool res_last_sl);
}


struct test_fs_elem {

   map<string, test_fs_elem *> c; /* children */
};

#define ROOT_NODE(...) new test_fs_elem{{ __VA_ARGS__ }}
#define NODE(name, ...) make_pair(name, ROOT_NODE( __VA_ARGS__ ))

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
      )
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

void
testfs1_get_entry(filesystem *fs,
                  void *dir_inode,
                  const char *name,
                  ssize_t name_len,
                  fs_path_struct *fs_path)
{
   if (!name) {
      fs_path->type = VFS_DIR;
      fs_path->inode = fs->device_data;
      fs_path->dir_inode = fs->device_data;
      fs_path->dir_entry = nullptr;
      return;
   }

   string s{name, (size_t)name_len};
   //cout << "get_entry: '" << s << "'" << endl;

   test_fs_elem *e = (test_fs_elem *)dir_inode;
   auto it = e->c.find(s);

   if (it != e->c.end()) {
      fs_path->inode = it->second;
      fs_path->type = name[0] == 'f' ? VFS_FILE : VFS_DIR;
   } else {
      fs_path->inode = nullptr;
      fs_path->type = VFS_NONE;
   }

   fs_path->dir_inode = e;
}

static void vfs_test_fs_nolock(filesystem *) { }
static int vfs_test_rri(filesystem *, vfs_inode_ptr_t) { return 1; }

/*
 * Unfortunately, in C++ non-trivial designated initializers are not supported,
 * so we have to explicitly initialize all the members, in order!
 */
static const fs_ops static_fsops_testfs1 = {

   testfs1_get_entry,   /* get_entry */
   NULL,                /* get_inode */
   NULL,                /* open */
   NULL,                /* close */
   NULL,                /* dup */
   NULL,                /* getdents */
   NULL,                /* unlink */
   NULL,                /* stat */
   NULL,                /* mkdir */
   NULL,                /* rmdir */
   NULL,                /* symlink */
   NULL,                /* readlink */
   NULL,                /* truncate */
   vfs_test_rri,        /* retain_inode */
   vfs_test_rri,        /* release_inode */
   vfs_test_fs_nolock,  /* fs_exlock */
   vfs_test_fs_nolock,  /* fs_exunlock */
   vfs_test_fs_nolock,  /* fs_shlock */
   vfs_test_fs_nolock,  /* fs_shunlock */
};

static filesystem testfs1 = {

   1,                        /* ref-count */
   "testfs1",                /* fs type name */
   0,                        /* device_id */
   0,                        /* flags */
   fs1_root,                 /* device_data */
   &static_fsops_testfs1,    /* fsops */
};

static filesystem testfs2 = {

   1,                        /* ref-count */
   "testfs2",                /* fs type name */
   0,                        /* device_id */
   0,                        /* flags */
   fs2_root,                 /* device_data */
   &static_fsops_testfs1,    /* fsops */
};


static int resolve(const char *path, vfs_path *p, bool res_last_sl)
{
   if (*path == '/') {
      bzero(p, sizeof(*p));
      p->fs = &testfs1;
      retain_obj(p->fs);
      testfs1_get_entry(p->fs, nullptr, nullptr, 0, &p->fs_path);
   }

   return __vfs_resolve(path, p, true, res_last_sl);
}

TEST(vfs_resolve, basic_test)
{
   int rc;
   vfs_path p;

   create_kernel_process();

   /* root path */
   rc = resolve("/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.dir_inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   /* regular 1-level path */
   rc = resolve("/a", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]);
   ASSERT_TRUE(p.fs_path.dir_inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   /* non-existent 1-level path */
   rc = resolve("/x", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == nullptr);
   ASSERT_TRUE(p.fs_path.dir_inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_NONE);

   /* regular 2-level path */
   rc = resolve("/a/b", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   /* regular 2-level path + trailing slash */
   rc = resolve("/a/b/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   /* 2-level path with non-existent component in the middle */
   rc = resolve("/x/b", &p, true);
   ASSERT_EQ(rc, -ENOENT);

   /* 4-level path ending with file */
   rc = resolve("/a/b/c/f1", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]->c["c"]->c["f1"]);
   ASSERT_TRUE(p.fs_path.dir_inode == fs1_root->c["a"]->c["b"]->c["c"]);
   ASSERT_TRUE(p.fs_path.type == VFS_FILE);

   /* 4-level path ending with file + trailing slash */
   rc = resolve("/a/b/c/f1/", &p, true);
   ASSERT_EQ(rc, -ENOTDIR);
}

TEST(vfs_resolve, corner_cases)
{
   int rc;
   vfs_path p;

   /* empty path */
   rc = resolve("", &p, true);
   ASSERT_EQ(rc, -ENOENT);

   /* multiple slashes [root] */
   rc = resolve("/////", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   /* multiple slashes [in the middle] */
   rc = resolve("/a/b/c////f1", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]->c["c"]->c["f1"]);
   ASSERT_TRUE(p.fs_path.dir_inode == fs1_root->c["a"]->c["b"]->c["c"]);
   ASSERT_TRUE(p.fs_path.type == VFS_FILE);

   /* multiple slashes [at the beginning] */
   rc = resolve("//a/b/c/f1", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]->c["c"]->c["f1"]);
   ASSERT_TRUE(p.fs_path.type == VFS_FILE);

   /* multiple slashes [at the end] */
   rc = resolve("/a/b/////", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   /* dir entry starting with '.' */
   rc = resolve("/a/b/.hdir", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]->c[".hdir"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_TRUE(p.fs_path.dir_inode == fs1_root->c["a"]->c["b"]);

   /* dir entry starting with '.' + trailing slash */
   rc = resolve("/a/b/.hdir/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]->c[".hdir"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_TRUE(p.fs_path.dir_inode == fs1_root->c["a"]->c["b"]);
}

TEST(vfs_resolve, single_dot)
{
   int rc;
   vfs_path p;

   rc = resolve("/a/.", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/a/./", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/.", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/./", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/a/./b/c", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]->c["c"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
}

TEST(vfs_resolve, double_dot)
{
   int rc;
   vfs_path p;

   rc = resolve("/a/b/c/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/a/b/c/../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/a/b/c/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/a/b/c/../../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/a/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/a/../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/a/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
}

TEST(vfs_resolve_multi_fs, basic_case)
{
   int rc;
   vfs_path p;

   init_kmalloc_for_tests();
   create_kernel_process();
   mountpoint_reset();

   mountpoint_add(&testfs1, "/"); // older interface. TODO: remove
   mp2_init(&testfs1);
   mp2_add(&testfs2, "/a/b/c2");

   /* target-fs's root without slash */
   rc = resolve("/a/b/c2", &p, true);

   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root);

   /* target-fs's root with slash */
   rc = resolve("/a/b/c2/", &p, true);

   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root);

   rc = resolve("/a/b/c2/x", &p, true);

   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root->c["x"]);

   rc = resolve("/a/b/c2/f_fs2_1", &p, true);

   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root->c["f_fs2_1"]);

   rc = resolve("/a/b/c2/x/y", &p, true);

   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root->c["x"]->c["y"]);

   rc = resolve("/a/b/c2/x/y/z", &p, true);

   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root->c["x"]->c["y"]->c["z"]);

   rc = resolve("/a/b/c2/x/y/z/", &p, true);

   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root->c["x"]->c["y"]->c["z"]);
}

TEST(vfs_resolve_multi_fs, dot_dot)
{
   int rc;
   vfs_path p;

   init_kmalloc_for_tests();
   create_kernel_process();
   mountpoint_reset();

   mountpoint_add(&testfs1, "/"); // older interface. TODO: remove
   mp2_init(&testfs1);
   mp2_add(&testfs2, "/a/b/c2");

   rc = resolve("/a/b/c2/x/y/z/..", &p, true);

   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root->c["x"]->c["y"]);

   rc = resolve("/a/b/c2/x/y/z/../", &p, true);

   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root->c["x"]->c["y"]);

   rc = resolve("/a/b/c2/x/..", &p, true);

   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root);

   rc = resolve("/a/b/c2/x/../", &p, true);

   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root);
   ASSERT_TRUE(p.fs == &testfs2);

   /* ../ crossing the fs-boundary [c2 is a mount-point] */
   rc = resolve("/a/b/c2/x/../..", &p, true);

   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]);
   ASSERT_TRUE(p.fs == &testfs1);
}
