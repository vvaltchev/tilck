
#pragma once

#include <fs/exvfs.h>

typedef struct {

   /* fs_handle_base */
   filesystem *fs;
   file_ops fops;

   /* specific fields */
   int id;

} devfs_file_handle;


filesystem *create_devfs(void);
