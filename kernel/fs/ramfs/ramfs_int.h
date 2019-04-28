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

#define RAMFS_ENTRY_MAX_LEN \
   (256 - sizeof(bintree_node) - sizeof(list_node) - sizeof(void *))

typedef struct {

   bintree_node node;
   list_node lnode;
   struct ramfs_inode *inode;
   char name[RAMFS_ENTRY_MAX_LEN];

} ramfs_entry;

STATIC_ASSERT(sizeof(ramfs_entry) == 256);

struct ramfs_inode {

   /*
    * Inode's ref-count is number of file handles currently pointing to this
    * inode.
    */
   REF_COUNTED_OBJECT;

   tilck_inode_t ino;
   enum vfs_entry_type type;
   rwlock_wp rwlock;
   nlink_t nlink;
   mode_t mode;
   size_t blocks_count;                /* count of page-size blocks */
   struct ramfs_inode *parent_dir;

   union {

      /* valid when type == VFS_FILE */
      struct {
         off_t fsize;
         ramfs_block *blocks_tree_root;
      };

      /* valid when type == VFS_DIR */
      struct {
         off_t num_entries;
         ramfs_entry *entries_tree_root;
         list entries_list;
      };

      /* valid when type == VFS_SYMLINK */
      struct {
         off_t path_len;
         const char *path;
      };
   };

   time_t ctime;
   time_t mtime;
};

typedef struct {

   /* fs_handle_base */
   FS_HANDLE_BASE_FIELDS

   /* ramfs-specific fields */
   ramfs_inode *inode;
   ramfs_entry *dpos;            /* valid only if inode->type == VFS_DIR */
   off_t pos;

} ramfs_handle;

typedef struct {

   rwlock_wp rwlock;

   tilck_inode_t next_inode_num;
   ramfs_inode *root;

} ramfs_data;

CREATE_FS_PATH_STRUCT(ramfs_path, ramfs_inode *, ramfs_entry *);
