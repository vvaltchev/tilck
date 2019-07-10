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
   #include "kernel/fs/fs_int.h"

   void mountpoint_reset(void);

   int __vfs_resolve(vfs_resolve_int_ctx *ctx, bool res_last_sl);
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
testfs_get_entry(filesystem *fs,
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
static const fs_ops static_fsops_testfs = {

   testfs_get_entry,    /* get_entry */
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


static int resolve(const char *path, vfs_path *p, bool res_last_sl)
{
   int rc;

   if (*path == '/' || !*path) {
      bzero(p, sizeof(*p));
      p->fs = &testfs1;
      retain_obj(p->fs);
      testfs_get_entry(p->fs, nullptr, nullptr, 0, &p->fs_path);
   }

   vfs_resolve_int_ctx ctx;
   ctx.orig_path = path,
   ctx.ss = 0,
   ctx.exlock = true,

   __vfs_resolve_stack_push(&ctx, ctx.orig_path, p);

   rc = __vfs_resolve(&ctx, res_last_sl);
   assert(ctx.ss >= 1);

   *p = ctx.paths[ctx.ss - 1];

   for (int i = ctx.ss - 1; i >= 0; i--) {
      filesystem *fs = ctx.paths[i].fs;
      if (ctx.paths[i].fs_path.inode)
         fs->fsops->release_inode(fs, ctx.paths[i].fs_path.inode);
   }

   return rc;
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

TEST(vfs_resolve, corner_cases)
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

TEST(vfs_resolve, single_dot)
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

TEST(vfs_resolve, double_dot)
{
   int rc;
   vfs_path p;

   rc = resolve("/a/b/c/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "c/..");

   rc = resolve("/a/b/c/../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "c/../");

   rc = resolve("/a/b/c/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "b/c/../..");

   rc = resolve("/a/b/c/../../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "b/c/../../");

   rc = resolve("/a/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "a/..");

   rc = resolve("/a/../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   //ASSERT_STREQ(p.last_comp, ""); // TODO: fix this!!

   rc = resolve("/a/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   //ASSERT_STREQ(p.last_comp, ""); // TODO: fix this!!

   rc = resolve("/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   //ASSERT_STREQ(p.last_comp, ""); // TODO: fix this!!

   rc = resolve("/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs1_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   //ASSERT_STREQ(p.last_comp, ""); // TODO: fix this!!
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
   ASSERT_STREQ(p.last_comp, "z/..");

   rc = resolve("/a/b/c2/x/y/z/../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root->c["x"]->c["y"]);
   ASSERT_STREQ(p.last_comp, "z/../");

   rc = resolve("/a/b/c2/x/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root);
   ASSERT_STREQ(p.last_comp, "c2/x/..");

   rc = resolve("/a/b/c2/x/../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs2_root);
   ASSERT_TRUE(p.fs == &testfs2);
   ASSERT_STREQ(p.last_comp, "c2/x/../");

   /* ../ crossing the fs-boundary [c2 is a mount-point] */
   rc = resolve("/a/b/c2/x/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != NULL);
   ASSERT_TRUE(p.fs_path.inode == fs1_root->c["a"]->c["b"]);
   ASSERT_TRUE(p.fs == &testfs1);
   ASSERT_STREQ(p.last_comp, "b/c2/x/../..");
}
