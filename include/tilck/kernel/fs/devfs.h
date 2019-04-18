/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/list.h>

#define DEVFS_READ_BS   4096
#define DEVFS_WRITE_BS  4096

enum devfs_entry {
   DEVFS_DIRECTORY,
   DEVFS_CHAR_DEVICE,
};

typedef struct {

   /* fs_handle_base */
   FS_HANDLE_BASE_FIELDS

   /* devfs-specific fields */

   enum devfs_entry type;
   void *devfs_file_ptr;

   u32 read_pos;
   u32 write_pos;
   u32 read_buf_used;
   u32 write_buf_used;

   char *read_buf;
   char *write_buf;

   bool read_allowed_to_return;
   bool write_allowed_to_return;

} devfs_file_handle;

typedef struct {

   list_node dir_node;

   u16 dev_major;
   u16 dev_minor;
   const char *name;
   const file_ops *fops;
   enum devfs_entry type;

} devfs_file;

typedef int
(*func_create_device_file)(int, const file_ops **, enum devfs_entry *);

typedef struct {

   u16 major;
   const char *name;
   func_create_device_file create_dev_file;

} driver_info;


filesystem *create_devfs(void);
void init_devfs(void);
int register_driver(driver_info *info, int major);

int create_dev_file(const char *filename, u16 major, u16 minor);
filesystem *get_devfs(void);
driver_info *get_driver_info(u16 major);

int devfs_char_dev_stat64(fs_handle h, struct stat64 *statbuf);
