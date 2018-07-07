
#pragma once
#include <exos/kernel/fs/exvfs.h>

#define DEVFS_READ_BUF_SIZE   4096
#define DEVFS_WRITE_BUF_SIZE  4096

typedef enum {

   DEVFS_DIRECTORY,
   DEVFS_CHAR_DEVICE

} devfs_entry_type;

typedef struct {

   /* fs_handle_base */
   filesystem *fs;
   file_ops fops;

   /* devfs-specific fields */

   devfs_entry_type type;
   void *devfs_file_ptr;

   u32 read_pos;
   u32 write_pos;
   u32 read_buf_used;
   u32 write_buf_used;

   void *read_buf;
   void *write_buf;

} devfs_file_handle;


typedef int (*func_create_device_file)(int, file_ops *, devfs_entry_type *);

typedef struct {

   const char *name;
   func_create_device_file create_dev_file;

} driver_info;


filesystem *create_devfs(void);
void create_and_register_devfs(void);
int register_driver(driver_info *info);

int create_dev_file(const char *filename, int major, int minor);
