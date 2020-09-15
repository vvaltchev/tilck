/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/kernel/fs/vfs_base.h>
#include <tilck/kernel/sys_types.h>
#include <tilck/kernel/list.h>

#define DEVFS_READ_BS   4096
#define DEVFS_WRITE_BS  4096

struct devfs_file_info {

   const struct file_ops *fops;
   u16 spec_flags;
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

         offt read_pos;
         offt write_pos;

         offt read_buf_used;
         offt write_buf_used;

         char *read_buf;
         char *write_buf;

         bool read_allowed_to_return;
         bool write_allowed_to_return;
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
