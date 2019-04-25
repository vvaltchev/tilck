/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/rwlock.h>
#include <tilck/kernel/datetime.h>
#include <tilck/kernel/bintree.h>
#include <tilck/kernel/paging.h>

#include <dirent.h> // system header

struct ramfs_inode;
typedef struct ramfs_inode ramfs_inode;

typedef struct {

   bintree_node node;
   off_t offset;                  /* MUST BE divisible by PAGE_SIZE */
   void *vaddr;

} ramfs_block;

#define RAMFS_ENTRY_MAX_LEN (256 - sizeof(bintree_node) - sizeof(void *))

typedef struct {

   bintree_node node;
   struct ramfs_inode *inode;
   char name[RAMFS_ENTRY_MAX_LEN];

} ramfs_entry;

struct ramfs_inode {

   /*
    * Inode's ref-count is number of file handles currently pointing to this
    * inode.
    */
   REF_COUNTED_OBJECT;

   int inode;
   enum vfs_entry_type type;
   nlink_t nlink;
   mode_t mode;
   rwlock_wp rwlock;
   size_t blocks_count;                /* count of page-size blocks */
   struct ramfs_inode *parent_dir;

   union {
      off_t fsize;                     /* valid when type == VFS_FILE */
      off_t num_entries;               /* valid when type == VFS_DIR */
      off_t path_len;                  /* valid when type == VFS_SYMLINK */
   };

   union {
      ramfs_block *blocks_tree_root;   /* valid when type == VFS_FILE */
      ramfs_entry *entries_tree_root;  /* valid when type == VFS_DIR */
      const char *path;                /* valid when type == VFS_SYMLINK */
   };

   time_t ctime;
   time_t mtime;
};

typedef struct {

   /* fs_handle_base */
   FS_HANDLE_BASE_FIELDS

   /* ramfs-specific fields */
   ramfs_inode *inode;
   off_t pos;

} ramfs_handle;

typedef struct {

   rwlock_wp rwlock;

   int next_inode_num;
   ramfs_inode *root;

} ramfs_data;

CREATE_FS_PATH_STRUCT(ramfs_path, ramfs_inode *, ramfs_entry *);
