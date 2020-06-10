/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/atomics.h>

#include <tilck/kernel/hal_types.h>

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

#define VFS_FS_RW             (1 << 0)  /* struct fs mounted in RW mode */
#define VFS_FS_RQ_DE_SKIP     (1 << 1)  /* FS requires vfs dents skip */

/* This struct is Tilck's analogue of Linux's "superblock" */
struct fs {

   REF_COUNTED_OBJECT;

   const char *fs_type_name; /* statically allocated: do NOT free() */
   u32 device_id;
   u32 flags;
   void *device_data;
   const struct fs_ops *fsops;
};


/*
 * Each fs_handle struct should contain at its beginning the fields of the
 * following base struct [a rough attempt to emulate inheritance in C].
 *
 * TODO: introduce a ref-count in the fs_base_handle struct when implementing
 * thread support.
 */

#define FS_HANDLE_BASE_FIELDS    \
   struct process *pi;           \
   struct fs *fs;                \
   const struct file_ops *fops;  \
   int fd_flags;                 \
   int fl_flags;                 \
   int spec_flags;               \
   offt pos;                        /* file: offset, dir: opaque entry index */

struct fs_handle_base {
   FS_HANDLE_BASE_FIELDS
};

/* File handle's special flags (spec_flags) */
#define VFS_SPFL_NO_USER_COPY         (1 << 0)
#define VFS_SPFL_MMAP_SUPPORTED       (1 << 1)

/*
 * vfs_mmap()'s flags
 *
 * By default the mmap() function (called by vfs_mmap) is expected to both do
 * the actual memory-map and to register the user mapping in inode's
 * mappings_list (not all file-systems do that, e.g. ramfs does, fat doesn't).
 * For more about where the mappings_list play a role in ramfs, see the func
 * ramfs_unmap_past_eof_mappings().
 *
 * However, in certain contexts, like partial un-mapping we might want to just
 * register the new user-mapping, without actually doing it. That's where the
 * VFS_MM_DONT_MMAP flag play a role. At the same way, in other exceptional
 * situations we might not want the FS to register the mapping, but to do it
 * anyway.
 */
#define VFS_MM_DONT_MMAP            (1 << 0)
#define VFS_MM_DONT_REGISTER        (1 << 1)

int vfs_mmap(struct user_mapping *um, pdir_t *pdir, int flags);
int vfs_munmap(fs_handle h, void *vaddr, size_t len);
bool vfs_handle_fault(fs_handle h, void *va, bool p, bool rw);
int vfs_dup(fs_handle h, fs_handle *dup_h);
void vfs_close2(struct process *pi, fs_handle h);
void vfs_close(fs_handle h);
fs_handle get_fs_handle(int fd);

static ALWAYS_INLINE bool
is_mmap_supported(fs_handle h)
{
   struct fs_handle_base *hb = (struct fs_handle_base *)h;
   return !!(hb->spec_flags & VFS_SPFL_MMAP_SUPPORTED);
}
