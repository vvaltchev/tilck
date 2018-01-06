
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

typedef ssize_t off_t;

/*
 * Opaque type for file handles.
 *
 * The only requirement for such handles is that they must have at their
 * beginning all the members of fs_handle_base. Therefore, a fs_handle MUST
 * always be castable to fs_handle_base *.
 */
typedef void *fs_handle;

typedef struct filesystem filesystem;

typedef fs_handle (*func_open) (filesystem *, const char *);
typedef void (*func_close) (fs_handle);
typedef ssize_t (*func_read) (fs_handle, char *, size_t);
typedef ssize_t (*func_write) (fs_handle, char *, size_t);
typedef off_t (*func_seek) (fs_handle, off_t, int);

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

struct filesystem {

   void *device_data;

   func_open fopen;
   func_close fclose;
};

typedef struct {

   func_read fread;
   func_write fwrite;
   func_seek fseek;

} file_ops;

typedef struct {

   filesystem *fs;
   char path[0];

} mountpoint;

/*
 * Each fs_handle struct should contain at its beginning the fields of the
 * following base struct [a rough attempt to emulate inheritance in C].
 */
typedef struct {

   filesystem *fs;
   file_ops fops;

} fs_handle_base;


int mountpoint_add(filesystem *fs, const char *path);
void mountpoint_remove(filesystem *fs);

fs_handle exvfs_open(const char *path);
void exvfs_close(fs_handle h);
ssize_t exvfs_read(fs_handle h, void *buf, size_t buf_size);
ssize_t exvfs_write(fs_handle h, void *buf, size_t buf_size);
off_t exvfs_seek(fs_handle h, off_t off, int whence);

