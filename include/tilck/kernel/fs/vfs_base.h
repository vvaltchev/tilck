/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>

/* Forward declarations */
struct process;
struct user_mapping;
struct fs_ops;

/*
 * Opaque type for file handles.
 *
 * The only requirement for such handles is that they must have at their
 * beginning all the members of fs_handle_base. Therefore, a fs_handle MUST
 * always be castable to fs_handle_base *.
 */
typedef void *fs_handle;


/* Directory entry types */
enum vfs_entry_type {

   VFS_NONE       = 0,
   VFS_FILE       = 1,
   VFS_DIR        = 2,
   VFS_SYMLINK    = 3,
   VFS_CHAR_DEV   = 4,
   VFS_BLOCK_DEV  = 5,
   VFS_PIPE       = 6,
};


/*
 * VFS opaque inode pointer.
 *
 * It is the primary member the struct fs_path and it's used by functions like
 * (stat, fstat), (truncate, ftruncate) in order to have a common implementation
 * in the FS layer.
 */
typedef void *vfs_inode_ptr_t;

#define CREATE_FS_PATH_STRUCT(name, inode_type, fs_entry_type)            \
                                                                          \
   STATIC_ASSERT(sizeof(inode_type) == sizeof(vfs_inode_ptr_t));          \
   STATIC_ASSERT(sizeof(fs_entry_type) == sizeof(void *));                \
                                                                          \
   struct name {                                                          \
      inode_type inode;                                                   \
      inode_type dir_inode;                                               \
      fs_entry_type dir_entry;                                            \
      enum vfs_entry_type type;                                           \
   }                                                                      \

CREATE_FS_PATH_STRUCT(fs_path, vfs_inode_ptr_t, void *);

struct vfs_path {

   struct fs *fs;
   struct fs_path fs_path;

   /* other fields */
   const char *last_comp;
};

/* This struct is Tilck's analogue of Linux's "superblock" */
struct fs {

   REF_COUNTED_OBJECT;

   const char *fs_type_name; /* statically allocated: do NOT free() */
   u32 device_id;
   u32 flags;
   void *device_data;
   const struct fs_ops *fsops;
};
