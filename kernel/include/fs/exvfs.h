
#pragma once

/*
 * exOS's toy virtual file system
 *
 * As this project's goals are by far different from the Linux ones, this
 * layer won't provide anything close to the Linux's VFS. Its purpose is to
 * provide the MINIMUM NECESSARY to allow basic operations like open, read,
 * write, close to work both on FAT32 and on character devices like /dev/tty0
 * (when it will implemented). In particular:
 *
 *    - No real disk I/O will be supported
 *    - No disk cache
 *    - No access control: single user (root) system
 *    - No SMP
 *    - Only the simplest Linux syscalls will be supported
 *
 */

#include <common_defs.h>

typedef void *fs_handle;
typedef struct filesystem filesystem;

typedef fs_handle (*func_open) (filesystem *, const char *);
typedef void (*func_close) (filesystem *, fs_handle);
typedef ssize_t (*func_read) (filesystem *, fs_handle, char *, size_t);
typedef ssize_t (*func_write) (filesystem *, fs_handle, char *, size_t);

struct filesystem {

   void *device_data;

   func_open fopen;
   func_close fclose;
   func_read fread;
   func_write fwrite;

};

typedef struct {

   filesystem *fs;
   char path[0];

} mountpoint;
