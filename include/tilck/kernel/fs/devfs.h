/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/kernel/fs/vfs_base.h>
#include <tilck/kernel/sys_types.h>
#include <tilck/kernel/list.h>

typedef int (*func_create_per_handle_extra)(int minor, void *extra);
typedef int (*func_on_dup_per_handle_extra)(int minor, void *extra);
typedef void (*func_destroy_per_handle_extra)(int minor, void *extra);

#define DEVFS_EXTRA_SIZE            (7 * sizeof(void *))

struct devfs_file_info {

   const struct file_ops *fops;
   u16 spec_flags;

   func_create_per_handle_extra create_extra;
   func_on_dup_per_handle_extra on_dup_extra;
   func_destroy_per_handle_extra destroy_extra;
};

struct devfs_file {

   enum vfs_entry_type type;     /* Must be FIRST, because of devfs_dir */

   struct list_node dir_node;
   struct devfs_file_info nfo;

   u16 dev_major;
   u16 dev_minor;
   const char *name;
   tilck_ino_t inode;
};

struct devfs_handle {

   /* struct fs_handle_base */
   FS_HANDLE_BASE_FIELDS

   /* devfs-specific fields */
   enum vfs_entry_type type;

   union {

      struct devfs_file *dpos;               /* valid only if type == VFS_DIR */

      struct {
         struct devfs_file *file;            /* valid only if type != VFS_DIR */
         char extra[DEVFS_EXTRA_SIZE] ALIGNED_AT(sizeof(void *));
      };
   };

};

STATIC_ASSERT(sizeof(struct devfs_handle) <= MAX_FS_HANDLE_SIZE);

typedef int
(*func_create_device_file)(int minor,
                           enum vfs_entry_type *type,
                           struct devfs_file_info *nfo);

struct driver_info {

   u16 major;
   const char *name;
   func_create_device_file create_dev_file;
};


struct fs *create_devfs(void);
void init_devfs(void);
int register_driver(struct driver_info *info, int major);

int create_dev_file(const char *filename, u16 major, u16 minor, void **devfile);
struct fs *get_devfs(void);
struct driver_info *get_driver_info(u16 major);

/* Special interface for in-kernel use of devfs handles */
int devfs_kernel_create_handle_for(void *devfile, fs_handle *out);
void devfs_kernel_destory_handle(fs_handle h);
