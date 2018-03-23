
#pragma once
#include <exos/fs/exvfs.h>

typedef struct {

   /* fs_handle_base */
   filesystem *fs;
   file_ops fops;

} devfs_file_handle;


typedef int (*func_create_device_file)(int minor, file_ops *ops);

typedef struct {

   const char *name;
   func_create_device_file create_dev_file;

} driver_info;


filesystem *create_devfs(void);
void create_and_register_devfs(void);
int register_driver(driver_info *info);

int create_dev_file(const char *filename, int major, int minor);
