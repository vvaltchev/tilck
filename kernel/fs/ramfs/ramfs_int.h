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

enum ramfs_entry {
   RAMFS_FILE,
   RAMFS_DIRECTORY,
   RAMFS_SYMLINK,
};

typedef struct {

   bintree_node node;
   uptr offset;                  /* MUST BE divisible by PAGE_SIZE */
   void *vaddr;

} ramfs_block;

typedef struct {

   list_node node;
   struct ramfs_inode *inode;
   char name[256 - sizeof(list_node) - sizeof(void *)];

} ramfs_entry;

struct ramfs_inode {

   REF_COUNTED_OBJECT;

   int inode;
   enum ramfs_entry type;
   mode_t mode;                        /* permissions + special flags */
   rwlock_wp rwlock;
   ssize_t fsize;
   size_t blocks_count;

   union {
      ramfs_block *blocks_tree_root;   /* valid when type == RAMFS_FILE */
      list entries_list;               /* valid when type == RAMFS_DIRECTORY */
      const char *path;                /* valid when type == RAMFS_SYMLINK */
   };

   datetime_t ctime;
   datetime_t wtime;
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
