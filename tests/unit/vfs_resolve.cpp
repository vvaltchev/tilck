/* SPDX-License-Identifier: BSD-2-Clause */

#include <iostream>
#include <map>
#include "vfs_test_fs.h"

static tfs_entry *root1 =
   ROOT_NODE(
      N_DIR(
         "a",
         N_SYM("linkToOtherFs1", "./b/c2/fs2_1"),
         N_SYM("linkToOtherFs2", "./b/c2/x/l1"),
         N_SYM("p2", "b/p3"),
         N_DIR(
            "b",
            N_SYM("p3", "c/p4"),
            N_SYM("link_to_nowhere", "/a/b/x/y/z/blabla"),
            N_DIR(
               "c",
               N_SYM("p4", "./f1"),
               N_FILE("f1"),
               N_FILE("f2"),
               N_SYM("c2_rel_link", "../c2"),
               N_SYM("parent_of_c2_rel_link", "../c2/.."),
            ),
            N_DIR("c2"),            /* --> root2 */
            N_DIR(".hdir"),
         )
      ),
      N_DIR("dev"),                 /* --> root3 */
      N_SYM("abs_s1", "/a/b/c/f1"),
      N_SYM("rel_s1", "a/b/c/f1"),
      N_SYM("l0", "l1"),
      N_SYM("l1", "l0"),
      N_SYM("p0", "p1"),
      N_SYM("p1", "a/p2"),
   );

static tfs_entry *root2 =
   ROOT_NODE(
      N_DIR(
         "x",
         N_SYM("l1", "/a"),
         N_DIR(
            "y",
            N_DIR("z")
         )
      ),
      N_FILE("fs2_1"),
      N_FILE("fs2_2")
   );

static tfs_entry *root3 =
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

static filesystem fs1 = create_test_fs("fs1", root1);
static filesystem fs2 = create_test_fs("fs2", root2);
static filesystem fs3 = create_test_fs("fs3", root3);

static void reset_all_fs_refcounts()
{
   root1->reset_refcounts();
   root2->reset_refcounts();
   root3->reset_refcounts();

   fs1.ref_count = 1;
   fs2.ref_count = 1;
   fs3.ref_count = 1;
}

static void check_all_fs_refcounts()
{
   test_fs_check_refcounts(root1);
   test_fs_check_refcounts(root2);
   test_fs_check_refcounts(root3);

   ASSERT_EQ(fs1.ref_count, 5);
   ASSERT_EQ(fs2.ref_count, 2);
   ASSERT_EQ(fs3.ref_count, 2);
}

static int resolve(const char *path, vfs_path *p, bool res_last_sl)
{
   int rc;

   if ((rc = vfs_resolve(path, p, true, res_last_sl)) < 0)
      return rc;

   vfs_fs_exunlock(p->fs);
   release_obj(p->fs);
   return rc;
}

class vfs_resolve_test : public vfs_test_base {

protected:

   void SetUp() override {

      vfs_test_base::SetUp();

      mp2_init(&fs1);
      mp2_add(&fs2, "/a/b/c2");
      mp2_add(&fs3, "/dev");

      test_fs_register_mp(path(root1, {"a", "b", "c2"}), root2);
      test_fs_register_mp(path(root1, {"dev"}), root3);

      {
         /*
          * The kernel_process does not have `cwd` set.
          * That is set in a lazy way on the first vfs_resolve() call.
          * TODO: make `cwd` to be always set.
          */
         vfs_path *tp = &get_curr_task()->pi->cwd;
         tp->fs = mp2_get_root();
         vfs_get_root_entry(tp->fs, &tp->fs_path);
         retain_obj(tp->fs);
         vfs_retain_inode_at(tp);
      }

      ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });
   }

   void TearDown() override {

      ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

      release_obj(mp2_get_root());

      /*
       * Compensate the effect of mp2_init, mp2_add etc.
       * TODO: implement and call mp2_remove() for each fs instead.
       */

      release_obj(&fs1); /* mp2_init(&fs1) */
      release_obj(&fs1); /* mp2_add(&fs2,...) */
      release_obj(&fs1); /* mp2_add(&fs3,...) */
      release_obj(&fs2);
      release_obj(&fs3);

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

   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* root path */
   rc = resolve("/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == root1);
   ASSERT_TRUE(p.fs_path.dir_inode == root1);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "/");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* regular 1-level path */
   rc = resolve("/a", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a"}));
   ASSERT_TRUE(p.fs_path.dir_inode == root1);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "a");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* non-existent 1-level path */
   rc = resolve("/x", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == nullptr);
   ASSERT_TRUE(p.fs_path.dir_inode == root1);
   ASSERT_TRUE(p.fs_path.type == VFS_NONE);
   ASSERT_STREQ(p.last_comp, "x");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* regular 2-level path */
   rc = resolve("/a/b", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a", "b"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "b");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* regular 2-level path + trailing slash */
   rc = resolve("/a/b/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a", "b"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "b/");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* 2-level path with non-existent component in the middle */
   rc = resolve("/x/b", &p, true);
   ASSERT_EQ(rc, -ENOENT);
   ASSERT_TRUE(p.fs_path.inode == nullptr);
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* 4-level path ending with file */
   rc = resolve("/a/b/c/f1", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a", "b", "c", "f1"}));
   ASSERT_TRUE(p.fs_path.dir_inode == path(root1, {"a", "b", "c"}));
   ASSERT_TRUE(p.fs_path.type == VFS_FILE);
   ASSERT_STREQ(p.last_comp, "f1");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* 4-level path ending with file + trailing slash */
   rc = resolve("/a/b/c/f1/", &p, true);
   ASSERT_EQ(rc, -ENOTDIR);
   ASSERT_STREQ(p.last_comp, nullptr);
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });
}

TEST_F(vfs_resolve_test, corner_cases)
{
   int rc;
   vfs_path p;

   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* empty path */
   rc = resolve("", &p, true);
   ASSERT_EQ(rc, -ENOENT);
   ASSERT_STREQ(p.last_comp, nullptr);
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* multiple slashes [root] */
   rc = resolve("/////", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == root1);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "/////");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* multiple slashes [in the middle] */
   rc = resolve("/a/b/c////f1", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a", "b", "c", "f1"}));
   ASSERT_TRUE(p.fs_path.dir_inode == path(root1, {"a", "b", "c"}));
   ASSERT_TRUE(p.fs_path.type == VFS_FILE);
   ASSERT_STREQ(p.last_comp, "f1");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* multiple slashes [at the beginning] */
   rc = resolve("//a/b/c/f1", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a", "b", "c", "f1"}));
   ASSERT_TRUE(p.fs_path.type == VFS_FILE);
   ASSERT_STREQ(p.last_comp, "f1");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* multiple slashes [at the end] */
   rc = resolve("/a/b/////", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a", "b"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "b/////");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* dir entry starting with '.' */
   rc = resolve("/a/b/.hdir", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a", "b", ".hdir"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_TRUE(p.fs_path.dir_inode == path(root1, {"a", "b"}));
   ASSERT_STREQ(p.last_comp, ".hdir");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* dir entry starting with '.' + trailing slash */
   rc = resolve("/a/b/.hdir/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a", "b", ".hdir"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_TRUE(p.fs_path.dir_inode == path(root1, {"a", "b"}));
   ASSERT_STREQ(p.last_comp, ".hdir/");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });
}

TEST_F(vfs_resolve_test, single_dot)
{
   int rc;
   vfs_path p;

   rc = resolve("/a/.", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, ".");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/./", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "./");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/.", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == root1);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, ".");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/./", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == root1);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "./");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/./b/c", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a", "b", "c"}));
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
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a", "b"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "..");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c/../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a", "b"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "../");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "..");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c/../../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a"}));
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "../");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c/../../new", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == nullptr);
   ASSERT_TRUE(p.fs_path.dir_inode == path(root1, {"a"}));
   ASSERT_TRUE(p.fs_path.type == VFS_NONE);
   ASSERT_STREQ(p.last_comp, "new");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c/../../new/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == nullptr);
   ASSERT_TRUE(p.fs_path.dir_inode == path(root1, {"a"}));
   ASSERT_TRUE(p.fs_path.type == VFS_NONE);
   ASSERT_STREQ(p.last_comp, "new/");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == root1);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "..");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == root1);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "../");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == root1);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "..");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == root1);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "..");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == root1);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_STREQ(p.last_comp, "..");
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
   ASSERT_TRUE(p.fs_path.inode == root2);
   ASSERT_STREQ(p.last_comp, "c2");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* target-fs's root with slash */
   rc = resolve("/a/b/c2/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == root2);
   ASSERT_STREQ(p.last_comp, "c2/");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c2/x", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == path(root2, {"x"}));
   ASSERT_STREQ(p.last_comp, "x");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c2/fs2_1", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == path(root2, {"fs2_1"}));
   ASSERT_STREQ(p.last_comp, "fs2_1");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c2/x/y", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == path(root2, {"x", "y"}));
   ASSERT_STREQ(p.last_comp, "y");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c2/x/y/z", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == path(root2, {"x", "y", "z"}));
   ASSERT_STREQ(p.last_comp, "z");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c2/x/y/z/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == path(root2, {"x", "y", "z"}));
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
   ASSERT_TRUE(p.fs_path.inode == path(root2, {"x", "y"}));
   ASSERT_STREQ(p.last_comp, "..");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c2/x/y/z/../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == path(root2, {"x", "y"}));
   ASSERT_STREQ(p.last_comp, "../");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* new file after '..' */
   rc = resolve("/a/b/c2/x/y/z/../new_file", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == nullptr);
   ASSERT_TRUE(p.fs_path.dir_inode == path(root2, {"x", "y"}));
   ASSERT_STREQ(p.last_comp, "new_file");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* new dir after '..' */
   rc = resolve("/a/b/c2/x/y/z/../new_dir/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == nullptr);
   ASSERT_TRUE(p.fs_path.dir_inode == path(root2, {"x", "y"}));
   ASSERT_STREQ(p.last_comp, "new_dir/");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c2/x/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == root2);
   ASSERT_STREQ(p.last_comp, "..");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c2/x/../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == root2);
   ASSERT_TRUE(p.fs == &fs2);
   ASSERT_STREQ(p.last_comp, "../");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* ../ crossing the fs-boundary [c2 is a mount-point] */
   rc = resolve("/a/b/c2/x/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a", "b"}));
   ASSERT_TRUE(p.fs == &fs1);
   ASSERT_STREQ(p.last_comp, "..");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/dev/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == root1);
   ASSERT_TRUE(p.fs == &fs1);
   ASSERT_STREQ(p.last_comp, "..");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/dev/../a", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a"}));
   ASSERT_TRUE(p.fs == &fs1);
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });
}

TEST_F(vfs_resolve_multi_fs, rel_paths)
{
   int rc;
   vfs_path p;
   struct process_info *pi = get_curr_task()->pi;

   rc = resolve("/dev/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == root3);
   ASSERT_TRUE(p.fs == &fs3);
   ASSERT_STREQ(p.last_comp, "dev/");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   pi->cwd = p;
   bzero(&p, sizeof(p));

   rc = resolve(".", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == root3);
   ASSERT_TRUE(p.fs == &fs3);
   ASSERT_STREQ(p.last_comp, ".");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs_path.inode == root1);
   ASSERT_TRUE(p.fs == &fs1);
   ASSERT_STREQ(p.last_comp, "..");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });
}

TEST_F(vfs_resolve_symlinks, basic_tests)
{
   int rc;
   vfs_path p;

   rc = resolve("/abs_s1", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs == &fs1);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a", "b", "c", "f1"}));
   ASSERT_STREQ(p.last_comp, "abs_s1");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/rel_s1", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs == &fs1);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a", "b", "c", "f1"}));
   ASSERT_STREQ(p.last_comp, "rel_s1");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c/c2_rel_link", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs == &fs2);
   ASSERT_TRUE(p.fs_path.inode == root2);
   ASSERT_STREQ(p.last_comp, "c2_rel_link");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c/c2_rel_link/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs == &fs2);
   ASSERT_TRUE(p.fs_path.inode == root2);
   ASSERT_STREQ(p.last_comp, "c2_rel_link/");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/c/parent_of_c2_rel_link", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs == &fs1);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a", "b"}));
   ASSERT_STREQ(p.last_comp, "parent_of_c2_rel_link");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/b/link_to_nowhere/c/d/f", &p, true);
   ASSERT_EQ(rc, -ENOENT);
   ASSERT_TRUE(p.fs_path.inode == nullptr);
   ASSERT_TRUE(p.fs == nullptr);
   ASSERT_STREQ(p.last_comp, nullptr);
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });
}

TEST_F(vfs_resolve_symlinks, nested_symlinks)
{
   int rc;
   vfs_path p;

   rc = resolve("/a/p2", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs == &fs1);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a", "b", "c", "f1"}));
   ASSERT_STREQ(p.last_comp, "p2");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });
}

TEST_F(vfs_resolve_symlinks, real_eloop)
{
   int rc;
   vfs_path p;

   rc = resolve("/l0", &p, true);
   ASSERT_EQ(rc, -ELOOP);
   ASSERT_TRUE(p.fs == nullptr);
   ASSERT_TRUE(p.fs_path.inode == nullptr);
   ASSERT_TRUE(p.last_comp == nullptr);
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });
}

TEST_F(vfs_resolve_symlinks, too_many_links_eloop)
{
   int rc;
   vfs_path p;

   /* 1 plus max nested symlinks */
   rc = resolve("/p1", &p, true);
   ASSERT_EQ(rc, -ELOOP);
   ASSERT_TRUE(p.fs == nullptr);
   ASSERT_TRUE(p.fs_path.inode == nullptr);
   ASSERT_TRUE(p.last_comp == nullptr);
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   /* 2 plus max nested symlinks */
   rc = resolve("/p0", &p, true);
   ASSERT_EQ(rc, -ELOOP);
   ASSERT_TRUE(p.fs == nullptr);
   ASSERT_TRUE(p.fs_path.inode == nullptr);
   ASSERT_TRUE(p.last_comp == nullptr);
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });
}


TEST_F(vfs_resolve_symlinks, cross_fs_symlinks)
{
   int rc;
   vfs_path p;

   rc = resolve("/a/linkToOtherFs1", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs == &fs2);
   ASSERT_TRUE(p.fs_path.inode == path(root2, {"fs2_1"}));
   ASSERT_STREQ(p.last_comp, "linkToOtherFs1");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/linkToOtherFs2", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs == &fs1);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a"}));
   ASSERT_STREQ(p.last_comp, "linkToOtherFs2");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });

   rc = resolve("/a/linkToOtherFs2/b", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode != nullptr);
   ASSERT_TRUE(p.fs == &fs1);
   ASSERT_TRUE(p.fs_path.inode == path(root1, {"a", "b"}));
   ASSERT_STREQ(p.last_comp, "b");
   ASSERT_NO_FATAL_FAILURE({ check_all_fs_refcounts(); });
}
