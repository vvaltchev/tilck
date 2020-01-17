/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

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
#include <tilck/kernel/process_mm.h>

#include <dirent.h> // system header

struct ramfs_inode;

struct ramfs_block {

   struct bintree_node node;
   offt offset;                  /* MUST BE divisible by PAGE_SIZE */
   void *vaddr;
};

/*
 * Ramfs entries do not *necessarily* need to have a fixed size, as they are
 * allocated dynamically on the heap. Said that, a fixed-size entry struct is
 * simpler to manage and faster to alloc/free, in particular with Tilck's
 * current kmalloc implementation.
 */
#define RAMFS_ENTRY_SIZE 256
#define RAMFS_ENTRY_MAX_LEN (                   \
   RAMFS_ENTRY_SIZE                             \
   - sizeof(struct bintree_node)                \
   - sizeof(struct list_node)                   \
   - sizeof(struct ramfs_inode *)               \
   - sizeof(u8)                                 \
)

struct ramfs_entry {

   struct bintree_node node;
   struct list_node lnode;
   struct ramfs_inode *inode;
   u8 name_len;                     /* NOTE: includes the final \0 */
   char name[RAMFS_ENTRY_MAX_LEN];
};

STATIC_ASSERT(sizeof(struct ramfs_entry) == RAMFS_ENTRY_SIZE);

struct ramfs_inode {

   /*
    * Inode's ref-count is the number of file handles currently pointing to
    * this inode.
    */
   REF_COUNTED_OBJECT;

   tilck_ino_t ino;
   enum vfs_entry_type type;
   struct rwlock_wp rwlock;
   nlink_t nlink;
   mode_t mode;
   size_t blocks_count;                /* count of page-size blocks */
   struct ramfs_inode *parent_dir;
   struct list mappings_list;

   union {

      /* valid when type == VFS_FILE */
      struct {
         offt fsize;
         struct ramfs_block *blocks_tree_root;
      };

      /* valid when type == VFS_DIR */
      struct {
         offt num_entries;
         struct ramfs_entry *entries_tree_root;
         struct list entries_list;
         struct list handles_list;
      };

      /* valid when type == VFS_SYMLINK */
      struct {
         size_t path_len;
         char *path;
      };
   };

   /* TODO: consider introducing `atime`      */
   struct timespec mtime;
   struct timespec ctime;
};

struct ramfs_handle {

   /* struct fs_handle_base */
   FS_HANDLE_BASE_FIELDS

   /* ramfs-specific fields */
   struct ramfs_inode *inode;

   /* valid only if inode->type == VFS_DIR */
   struct {
      struct list_node node;        /* node in inode->handles_list */
      struct ramfs_entry *dpos;     /* current entry position */
   };
};

struct ramfs_data {

   struct rwlock_wp rwlock;

   tilck_ino_t next_inode_num;
   struct ramfs_inode *root;
};

CREATE_FS_PATH_STRUCT(ramfs_path, struct ramfs_inode *, struct ramfs_entry *);
