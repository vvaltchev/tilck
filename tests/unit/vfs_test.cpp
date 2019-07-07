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

   #include <tilck/kernel/fs/fat32.h>
   #include <tilck/kernel/fs/vfs.h>
   #include <tilck/kernel/sched.h>

   filesystem *ramfs_create(void);
   int __vfs_resolve(func_get_entry get_entry,
                     const char *path,
                     vfs_path *rp,
                     bool res_last_sl);
}

static int mountpoint_match_wrapper(const char *mp, const char *path)
{
   return mp_check_match(mp, strlen(mp), path, strlen(path));
}

// Implemented in fat32_test.cpp
const char *load_once_file(const char *filepath, size_t *fsize = nullptr);
void test_dump_buf(char *buf, const char *buf_name, int off, int count);

TEST(vfs, mp_check_match)
{
   EXPECT_EQ(mountpoint_match_wrapper("/", "/"), 1);
   EXPECT_EQ(mountpoint_match_wrapper("/", "/file"), 1);
   EXPECT_EQ(mountpoint_match_wrapper("/", "/dir1/file2"), 1);
   EXPECT_EQ(mountpoint_match_wrapper("/dev/", "/dev/tty0"), 5);
   EXPECT_EQ(mountpoint_match_wrapper("/devices/", "/dev"), 0);
   EXPECT_EQ(mountpoint_match_wrapper("/dev/", "/dev"), 4);
}

TEST(vfs, read_content_of_longname_file)
{
   init_kmalloc_for_tests();
   create_kernel_process();

   const char *buf = load_once_file(PROJ_BUILD_DIR "/test_fatpart");
   char data[128] = {0};

   filesystem *fat_fs = fat_mount_ramdisk((void *) buf, VFS_FS_RO);
   ASSERT_TRUE(fat_fs != NULL);

   int r = mountpoint_add(fat_fs, "/");
   ASSERT_EQ(r, 0);

   const char *file_path =
      "/testdir/This_is_a_file_with_a_veeeery_long_name.txt";

   fs_handle h = NULL;
   r = vfs_open(file_path, &h, 0, O_RDONLY);
   ASSERT_TRUE(r == 0);
   ASSERT_TRUE(h != NULL);
   ssize_t res = vfs_read(h, data, sizeof(data));
   vfs_close(h);

   EXPECT_GT(res, 0);
   ASSERT_STREQ("Content of file with a long name\n", data);

   mountpoint_remove(fat_fs);
   fat_umount_ramdisk(fat_fs);
}

TEST(vfs, fseek)
{
   init_kmalloc_for_tests();
   create_kernel_process();

   random_device rdev;
   const auto seed = rdev();
   default_random_engine engine(seed);
   lognormal_distribution<> dist(4.0, 3);
   off_t linux_lseek;
   off_t tilck_fseek;
   off_t linux_pos;
   off_t tilck_pos;

   cout << "[ INFO     ] random seed: " << seed << endl;

   size_t fatpart_size;
   const char *fatpart =
      load_once_file(PROJ_BUILD_DIR "/test_fatpart", &fatpart_size);

   filesystem *fat_fs = fat_mount_ramdisk((void *) fatpart, VFS_FS_RO);
   ASSERT_TRUE(fat_fs != NULL);

   int r = mountpoint_add(fat_fs, "/");
   ASSERT_EQ(r, 0);

   const char *fatpart_file_path = "/bigfile";
   const char *real_file_path = PROJ_BUILD_DIR "/test_sysroot/bigfile";

   int fd = open(real_file_path, O_RDONLY);

   size_t file_size = lseek(fd, 0, SEEK_END);

   fs_handle h = NULL;
   r = vfs_open(fatpart_file_path, &h, 0, O_RDONLY);
   ASSERT_TRUE(r == 0);
   ASSERT_TRUE(h != NULL);

   char buf_tilck[64];
   char buf_linux[64];

   linux_lseek = lseek(fd, file_size / 2, SEEK_SET);
   tilck_fseek = vfs_seek(h, file_size / 2, SEEK_SET);
   ASSERT_EQ(linux_lseek, tilck_fseek);

   off_t last_pos = lseek(fd, 0, SEEK_CUR);

   linux_pos = last_pos;
   tilck_pos = vfs_seek(h, 0, SEEK_CUR);
   ASSERT_EQ(linux_pos, tilck_pos);

   (void)last_pos;

   const int iters = 10000;

   for (int i = 0; i < iters; i++) {

      int saved_errno = 0;

      /* random file offset where to seek */
      off_t offset = (off_t) ( dist(engine) - dist(engine)/1.3 );

      linux_lseek = lseek(fd, offset, SEEK_CUR);

      if (linux_lseek < 0)
         saved_errno = errno;

      tilck_fseek = vfs_seek(h, offset, SEEK_CUR);

      linux_pos = lseek(fd, 0, SEEK_CUR);
      tilck_pos = vfs_seek(h, 0, SEEK_CUR);

      if (linux_lseek < 0) {

         /*
          * Linux syscalls return -ERRNO_VALUE in case something goes wrong,
          * while the glibc wrappers return -1 and set errno. Since we're
          * testing the value returned by the syscall in Tilck, we need to
          * revert that.
          */
         linux_lseek = -saved_errno;
      }

      ASSERT_EQ(tilck_fseek, linux_lseek)
         << "Offset: " << offset << endl
         << "Curr pos (glibc): " << linux_pos << endl
         << "Curr pos (tilck):  " << tilck_pos << endl;

      ASSERT_EQ(tilck_pos, linux_pos);

      memset(buf_linux, 0, sizeof(buf_linux));
      memset(buf_tilck, 0, sizeof(buf_tilck));

      ssize_t linux_read = read(fd, buf_linux, sizeof(buf_linux));
      ssize_t tilck_read = vfs_read(h, buf_tilck, sizeof(buf_tilck));

      ASSERT_EQ(tilck_read, linux_read);

      linux_pos = lseek(fd, 0, SEEK_CUR);
      tilck_pos = vfs_seek(h, 0, SEEK_CUR);

      ASSERT_EQ(tilck_pos, linux_pos);

      if (memcmp(buf_tilck, buf_linux, sizeof(buf_linux)) != 0) {

         cout << "Buffers differ. " << endl;
         cout << "Last offset: " << offset << endl;
         cout << "Curr pos: " << linux_pos << endl;
         cout << "read ret: " << linux_read << endl;

         test_dump_buf(buf_linux, "Linux buf:  ", 0, sizeof(buf_linux));
         test_dump_buf(buf_linux, "Tilck buf:  ", 0, sizeof(buf_linux));
         FAIL();
      }

      last_pos = linux_pos;
   }

   vfs_close(h);
   close(fd);

   mountpoint_remove(fat_fs);
   fat_umount_ramdisk(fat_fs);
}

static void create_test_file(int n)
{
   char path[256];
   fs_handle h;
   int rc;

   sprintf(path, "/test_%d", n);

   rc = vfs_open(path, &h, O_CREAT, 0644);
   ASSERT_EQ(rc, 0);

   vfs_close(h);
}

TEST(vfs_perf, creat)
{
   filesystem *fs;
   int rc;

   init_kmalloc_for_tests();
   create_kernel_process();

   fs = ramfs_create();

   ASSERT_TRUE(fs != NULL);

   rc = mountpoint_add(fs, "/");
   ASSERT_EQ(rc, 0);

   for (int i = 0; i < 100; i++)
      create_test_file(i);

   mountpoint_remove(fs);
   // TODO: destroy ramfs
}

string compute_abs_path_wrapper(const char *cwd, const char *path)
{
   char dest[256];
   int rc = compute_abs_path(path, cwd, dest, sizeof(dest));

   if (rc < 0)
      return "<error>";

   return dest;
}

TEST(compute_abs_path, tests)
{
   /* path is absolute */
   EXPECT_EQ(compute_abs_path_wrapper("/", "/a/b/c"), "/a/b/c");
   EXPECT_EQ(compute_abs_path_wrapper("/", "/a/b/c/"), "/a/b/c/");
   EXPECT_EQ(compute_abs_path_wrapper("/", "/a/b/c/.."), "/a/b");
   EXPECT_EQ(compute_abs_path_wrapper("/", "/a/b/c/../"), "/a/b/");

   /* path is relative */
   EXPECT_EQ(compute_abs_path_wrapper("/", "a/b/c"), "/a/b/c");
   EXPECT_EQ(compute_abs_path_wrapper("/", "a/b/c/"), "/a/b/c/");
   EXPECT_EQ(compute_abs_path_wrapper("/", "a/b/c/.."), "/a/b");
   EXPECT_EQ(compute_abs_path_wrapper("/", "a/b/c/../"), "/a/b/");

   /* path is relative starting with ./ */
   EXPECT_EQ(compute_abs_path_wrapper("/", "./a/b/c"), "/a/b/c");
   EXPECT_EQ(compute_abs_path_wrapper("/", "./a/b/c/"), "/a/b/c/");
   EXPECT_EQ(compute_abs_path_wrapper("/", "./a/b/c/.."), "/a/b");
   EXPECT_EQ(compute_abs_path_wrapper("/", "./a/b/c/../"), "/a/b/");

   /* path is relative, cwd != / */
   EXPECT_EQ(compute_abs_path_wrapper("/a/b/c/", "a"), "/a/b/c/a");
   EXPECT_EQ(compute_abs_path_wrapper("/a/b/c/", "a/"), "/a/b/c/a/");
   EXPECT_EQ(compute_abs_path_wrapper("/a/b/c/", ".."), "/a/b");
   EXPECT_EQ(compute_abs_path_wrapper("/a/b/c/", "../"), "/a/b/");
   EXPECT_EQ(compute_abs_path_wrapper("/a/b/c/", "../.."), "/a");
   EXPECT_EQ(compute_abs_path_wrapper("/a/b/c/", "../../"), "/a/");
   EXPECT_EQ(compute_abs_path_wrapper("/a/b/c/", "../../."), "/a");
   EXPECT_EQ(compute_abs_path_wrapper("/a/b/c/", "../.././"), "/a/");
   EXPECT_EQ(compute_abs_path_wrapper("/a/b/c/", "../../.."), "/");
   EXPECT_EQ(compute_abs_path_wrapper("/a/b/c/", "../../../"), "/");

   /* try to go beyond / */
   EXPECT_EQ(compute_abs_path_wrapper("/a/b/c/", "../../../.."), "/");
   EXPECT_EQ(compute_abs_path_wrapper("/a/b/c/", "../../../../"), "/");

   /* double slash */
   EXPECT_EQ(compute_abs_path_wrapper("/a/b/c/", "d//e"), "/a/b/c/d/e");

   /* triple slash */
   EXPECT_EQ(compute_abs_path_wrapper("/a/b/c/", "d///e"), "/a/b/c/d/e");

   /* other */
   EXPECT_EQ(compute_abs_path_wrapper("/a/b/c/", ".a"), "/a/b/c/.a");
   EXPECT_EQ(compute_abs_path_wrapper("/a/b/c/", "..a"), "/a/b/c/..a");
   EXPECT_EQ(compute_abs_path_wrapper("/", "something.."), "/something..");
   EXPECT_EQ(compute_abs_path_wrapper("/", "something."), "/something.");
}


/* -------- Tests for the vfs_resolve() function ----------- */

struct test_fs_elem {

   map<string, test_fs_elem *> c; /* children */
};

#define ROOT_NODE(...) new test_fs_elem{{ __VA_ARGS__ }}
#define NODE(name, ...) make_pair(name, ROOT_NODE( __VA_ARGS__ ))

static test_fs_elem *fs_root =
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

void
test_get_entry(filesystem *fs,
               void *dir_inode,
               const char *name,
               ssize_t name_len,
               fs_path_struct *fs_path)
{
   if (!name) {
      fs_path->type = VFS_DIR;
      fs_path->inode = (void *)fs_root;
      fs_path->dir_inode = (void *)fs_root;
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

static int resolve(const char *path, vfs_path *p, bool res_last_sl)
{
   if (*path == '/') {
      bzero(p, sizeof(*p));
      p->fs = (filesystem *)0xaabbccdd;
      test_get_entry(p->fs, nullptr, nullptr, 0, &p->fs_path);
   }

   return __vfs_resolve(&test_get_entry, path, p, res_last_sl);
}

TEST(vfs_resolve, basic_test)
{
   int rc;
   vfs_path p;

   create_kernel_process();

   /* root path */
   rc = resolve("/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root);
   ASSERT_TRUE(p.fs_path.dir_inode == fs_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   /* regular 1-level path */
   rc = resolve("/a", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root->c["a"]);
   ASSERT_TRUE(p.fs_path.dir_inode == fs_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   /* non-existent 1-level path */
   rc = resolve("/x", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == nullptr);
   ASSERT_TRUE(p.fs_path.dir_inode == fs_root);
   ASSERT_TRUE(p.fs_path.type == VFS_NONE);

   /* regular 2-level path */
   rc = resolve("/a/b", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root->c["a"]->c["b"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   /* regular 2-level path + trailing slash */
   rc = resolve("/a/b/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root->c["a"]->c["b"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   /* 2-level path with non-existent component in the middle */
   rc = resolve("/x/b", &p, true);
   ASSERT_EQ(rc, -ENOENT);

   /* 4-level path ending with file */
   rc = resolve("/a/b/c/f1", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root->c["a"]->c["b"]->c["c"]->c["f1"]);
   ASSERT_TRUE(p.fs_path.dir_inode == fs_root->c["a"]->c["b"]->c["c"]);
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
   ASSERT_TRUE(p.fs_path.inode == fs_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   /* multiple slashes [in the middle] */
   rc = resolve("/a/b/c////f1", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root->c["a"]->c["b"]->c["c"]->c["f1"]);
   ASSERT_TRUE(p.fs_path.dir_inode == fs_root->c["a"]->c["b"]->c["c"]);
   ASSERT_TRUE(p.fs_path.type == VFS_FILE);

   /* multiple slashes [at the beginning] */
   rc = resolve("//a/b/c/f1", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root->c["a"]->c["b"]->c["c"]->c["f1"]);
   ASSERT_TRUE(p.fs_path.type == VFS_FILE);

   /* multiple slashes [at the end] */
   rc = resolve("/a/b/////", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root->c["a"]->c["b"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   /* dir entry starting with '.' */
   rc = resolve("/a/b/.hdir", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root->c["a"]->c["b"]->c[".hdir"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_TRUE(p.fs_path.dir_inode == fs_root->c["a"]->c["b"]);

   /* dir entry starting with '.' + trailing slash */
   rc = resolve("/a/b/.hdir/", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root->c["a"]->c["b"]->c[".hdir"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
   ASSERT_TRUE(p.fs_path.dir_inode == fs_root->c["a"]->c["b"]);
}

TEST(vfs_resolve, single_dot)
{
   int rc;
   vfs_path p;

   rc = resolve("/a/.", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root->c["a"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/a/./", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root->c["a"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/.", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/./", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/a/./b/c", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root->c["a"]->c["b"]->c["c"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
}

TEST(vfs_resolve, double_dot)
{
   int rc;
   vfs_path p;

   rc = resolve("/a/b/c/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root->c["a"]->c["b"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/a/b/c/../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root->c["a"]->c["b"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/a/b/c/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root->c["a"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/a/b/c/../../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root->c["a"]);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/a/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/a/../", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/a/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);

   rc = resolve("/../..", &p, true);
   ASSERT_EQ(rc, 0);
   ASSERT_TRUE(p.fs_path.inode == fs_root);
   ASSERT_TRUE(p.fs_path.type == VFS_DIR);
}
