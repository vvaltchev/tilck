
#pragma once

/*
 * exOS's virtual file system
 *
 * As this project's goals are by far different from the Linux ones, this
 * layer won't provide anything close to the Linux's VFS. Its purpose is to
 * provide the MINIMUM NECESSARY to allow basic operations like open, read,
 * write, close to work both on FAT32 and on character devices like /dev/tty.
 * In particular:
 *
 *    - No real disk I/O will be supported
 *    - No disk cache
 *    - No access control: single user (root) system
 *    - No SMP
 *    - Only the simplest Linux syscalls will be supported
 *
 */

#include <common/basic_defs.h>
#include <exos/sys_types.h>

/*
 * Opaque type for file handles.
 *
 * The only requirement for such handles is that they must have at their
 * beginning all the members of fs_handle_base. Therefore, a fs_handle MUST
 * always be castable to fs_handle_base *.
 */
typedef void *fs_handle;

typedef struct filesystem filesystem;

typedef void (*func_close) (fs_handle);
typedef int (*func_open) (filesystem *, const char *, fs_handle *out);
typedef int (*func_ioctl) (fs_handle, uptr, void *);
typedef int (*func_stat) (fs_handle, struct stat *);

typedef ssize_t (*func_read) (fs_handle, char *, size_t);
typedef ssize_t (*func_write) (fs_handle, char *, size_t);
typedef off_t (*func_seek) (fs_handle, off_t, int);

typedef void (*func_fs_ex_lock)(filesystem *);
typedef void (*func_fs_ex_unlock)(filesystem *);

typedef fs_handle (*func_dup) (fs_handle);

struct filesystem {

   u32 device_id;
   void *device_data;

   func_open fopen;
   func_close fclose;
   func_dup dup;

   func_fs_ex_lock exlock;
   func_fs_ex_unlock exunlock;
};

typedef struct {

   func_read fread;
   func_write fwrite;
   func_seek fseek;
   func_ioctl ioctl;
   func_stat fstat;

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

int exvfs_open(const char *path, fs_handle *out);
int exvfs_ioctl(fs_handle h, uptr request, void *argp);
int exvfs_stat(fs_handle h, struct stat *statbuf);
int exvfs_dup(fs_handle h, fs_handle *dup_h);
void exvfs_close(fs_handle h);

ssize_t exvfs_read(fs_handle h, void *buf, size_t buf_size);
ssize_t exvfs_write(fs_handle h, void *buf, size_t buf_size);
off_t exvfs_seek(fs_handle h, off_t off, int whence);

void exvfs_exlock(fs_handle h);
void exvfs_exunlock(fs_handle h);

int
compute_abs_path(const char *path, const char *cwd, char *dest, u32 dest_size);

u32 exvfs_get_new_device_id(void);
