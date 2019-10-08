/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

/*
 * Tilck's virtual file system
 *
 * As this project's goals are by far different from the Linux ones, this
 * layer won't provide anything close to the Linux's VFS.
 */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/sys_types.h>
#include <tilck/kernel/sync.h>

typedef struct process_info process_info;
typedef struct user_mapping user_mapping;

/*
 * Opaque type for file handles.
 *
 * The only requirement for such handles is that they must have at their
 * beginning all the members of fs_handle_base. Therefore, a fs_handle MUST
 * always be castable to fs_handle_base *.
 */
typedef void *fs_handle;

typedef struct filesystem filesystem;

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
 * It is the primary member the fs_path_struct and it's used by functions like
 * (stat, fstat), (truncate, ftruncate) in order to have a common implementation
 * in the FS layer.
 */
typedef void *vfs_inode_ptr_t;

#define CREATE_FS_PATH_STRUCT(name, inode_type, fs_entry_type)            \
                                                                          \
   STATIC_ASSERT(sizeof(inode_type) == sizeof(vfs_inode_ptr_t));          \
   STATIC_ASSERT(sizeof(fs_entry_type) == sizeof(void *));                \
                                                                          \
   typedef struct {                                                       \
      inode_type inode;                                                   \
      inode_type dir_inode;                                               \
      fs_entry_type dir_entry;                                            \
      enum vfs_entry_type type;                                           \
   } name                                                                 \

CREATE_FS_PATH_STRUCT(fs_path_struct, vfs_inode_ptr_t, void *);

typedef struct {

   filesystem *fs;
   fs_path_struct fs_path;

   /* other fields */
   const char *last_comp;

} vfs_path;

typedef struct {

   tilck_inode_t ino;
   enum vfs_entry_type type;
   u8 name_len;               /* NODE: includes the final '\0' */
   const char *name;

} vfs_dent64;

typedef int (*get_dents_func_cb) (vfs_dent64 *, void *);

/* fs ops */
typedef vfs_inode_ptr_t (*func_get_inode) (fs_handle);

typedef void    (*func_close)     (fs_handle);
typedef int     (*func_open)      (vfs_path *, fs_handle *, int, mode_t);
typedef int     (*func_dup)       (fs_handle, fs_handle *);
typedef int     (*func_getdents)  (fs_handle, get_dents_func_cb, void *);
typedef int     (*func_unlink)    (vfs_path *p);
typedef int     (*func_mkdir)     (vfs_path *p, mode_t);
typedef int     (*func_rmdir)     (vfs_path *p);
typedef int     (*func_symlink)   (const char *, vfs_path *);
typedef int     (*func_readlink)  (vfs_path *, char *);
typedef int     (*func_chmod)     (filesystem *, vfs_inode_ptr_t, mode_t);
typedef void    (*func_fslock_t)  (filesystem *);
typedef int     (*func_rr_inode)  (filesystem *, vfs_inode_ptr_t);
typedef int     (*func_2paths)    (filesystem *, vfs_path *, vfs_path *);

typedef func_2paths func_rename;
typedef func_2paths func_link;

typedef void    (*func_get_entry) (filesystem *fs,
                                   void *dir_inode,
                                   const char *name,
                                   ssize_t name_len,
                                   fs_path_struct *fs_path);

/* mixed fs/file ops */
typedef int     (*func_stat)   (filesystem *, vfs_inode_ptr_t, struct stat64 *);
typedef int     (*func_trunc)  (filesystem *, vfs_inode_ptr_t, offt);

/* file ops */
typedef ssize_t (*func_read)         (fs_handle, char *, size_t);
typedef ssize_t (*func_write)        (fs_handle, char *, size_t);
typedef offt    (*func_seek)         (fs_handle, offt, int);
typedef int     (*func_ioctl)        (fs_handle, uptr, void *);
typedef int     (*func_mmap)         (fs_handle, void *, size_t, int, size_t);
typedef int     (*func_munmap)       (fs_handle, void *, size_t);
typedef bool    (*func_handle_fault) (fs_handle, void *, bool, bool);
typedef int     (*func_fcntl)        (fs_handle, int, int);
typedef void    (*func_hlock_t)      (fs_handle);
typedef bool    (*func_rwe_ready)    (fs_handle);
typedef kcond  *(*func_get_rwe_cond) (fs_handle);

/* Used by the devices when want to remove any locking from a file */
#define vfs_file_nolock           NULL

#define VFS_FS_RO                  (0)  /* filesystem mounted in RO mode */
#define VFS_FS_RW             (1 << 0)  /* filesystem mounted in RW mode */
#define VFS_FS_RQ_DE_SKIP     (1 << 1)  /* FS requires vfs dents skip */

/*
 * Operations affecting the file system structure (directories, files, etc.).
 *
 * What are the fs-lock functions
 * ---------------------------------
 *
 * The four fs-lock funcs below are supposed to be implemented by each
 * filesystem in order to protect its tree structure from races, typically by
 * using a read-write lock under the hood. Yes, that means that for example two
 * creat() operations even in separate directories cannot happen at the same
 * time, on the same FS. But, given that Tilck does NOT support SMP, this
 * approach not only offers a great simplification, but it actually increases
 * the overall throughput of the system (fine-grain per-directory locking is
 * pretty expensive).
 */
typedef struct {

   func_get_entry get_entry;
   func_get_inode get_inode;
   func_open open;
   func_close close;
   func_dup dup;
   func_getdents getdents;
   func_unlink unlink;
   func_stat stat;
   func_mkdir mkdir;
   func_rmdir rmdir;
   func_symlink symlink;
   func_readlink readlink;
   func_trunc truncate;
   func_chmod chmod;
   func_rename rename;
   func_link link;
   func_rr_inode retain_inode;
   func_rr_inode release_inode;

   /* file system structure lock funcs */
   func_fslock_t fs_exlock;
   func_fslock_t fs_exunlock;
   func_fslock_t fs_shlock;
   func_fslock_t fs_shunlock;

} fs_ops;

/* This struct is Tilck's analogue of Linux's "superblock" */
struct filesystem {

   REF_COUNTED_OBJECT;

   const char *fs_type_name; /* statically allocated: do NOT free() */
   u32 device_id;
   u32 flags;
   void *device_data;
   const fs_ops *fsops;
};

typedef struct {

   /* mandatory */
   func_read read;
   func_write write;
   func_seek seek;
   func_ioctl ioctl;
   func_fcntl fcntl;

   /* optional funcs */
   func_mmap mmap;
   func_munmap munmap;
   func_handle_fault handle_fault;

   /* optional, r/w/e ready funcs */
   func_rwe_ready read_ready;
   func_rwe_ready write_ready;
   func_rwe_ready except_ready;       /* unfetched exceptional condition */
   func_get_rwe_cond get_rready_cond;
   func_get_rwe_cond get_wready_cond;
   func_get_rwe_cond get_except_cond;

   /* optional, per-file locks (use vfs_file_nolock, when appropriate) */
   func_hlock_t exlock;
   func_hlock_t exunlock;
   func_hlock_t shlock;
   func_hlock_t shunlock;

} file_ops;

/*
 * Each fs_handle struct should contain at its beginning the fields of the
 * following base struct [a rough attempt to emulate inheritance in C].
 *
 * TODO: introduce a ref-count in the fs_base_handle struct when implementing
 * thread support.
 */

#define FS_HANDLE_BASE_FIELDS    \
   filesystem *fs;               \
   const file_ops *fops;         \
   int fd_flags;                 \
   int fl_flags;                 \
   offt pos;                        /* file: offset, dir: opaque entry index */

typedef struct {

   FS_HANDLE_BASE_FIELDS

} fs_handle_base;


int vfs_stat64(const char *path, struct stat64 *statbuf, bool res_last_sl);
int vfs_open(const char *path, fs_handle *out, int flags, mode_t mode);
int vfs_unlink(const char *path);
int vfs_mkdir(const char *path, mode_t mode);
int vfs_rmdir(const char *path);
int vfs_truncate(const char *path, offt length);
int vfs_symlink(const char *target, const char *linkpath);
int vfs_readlink(const char *path, char *buf);
int vfs_chown(const char *path, int owner, int group, bool reslink);
int vfs_chmod(const char *path, mode_t mode);
int vfs_rename(const char *oldpath, const char *newpath);
int vfs_link(const char *oldpath, const char *newpath);

int vfs_ftruncate(fs_handle h, offt length);
int vfs_ioctl(fs_handle h, uptr request, void *argp);
int vfs_fstat64(fs_handle h, struct stat64 *statbuf);
int vfs_dup(fs_handle h, fs_handle *dup_h);
int vfs_getdents64(fs_handle h, struct linux_dirent64 *dirp, u32 bs);
int vfs_fcntl(fs_handle h, int cmd, int arg);
int vfs_mmap(user_mapping *um);
int vfs_munmap(fs_handle h, void *vaddr, size_t len);
int vfs_fchmod(fs_handle h, mode_t mode);
void vfs_close(fs_handle h);
void vfs_close2(process_info *pi, fs_handle h);
bool vfs_handle_fault(fs_handle h, void *va, bool p, bool rw);

bool vfs_read_ready(fs_handle h);
bool vfs_write_ready(fs_handle h);
bool vfs_except_ready(fs_handle h);
kcond *vfs_get_rready_cond(fs_handle h);
kcond *vfs_get_wready_cond(fs_handle h);
kcond *vfs_get_except_cond(fs_handle h);

ssize_t vfs_read(fs_handle h, void *buf, size_t buf_size);
ssize_t vfs_write(fs_handle h, void *buf, size_t buf_size);
offt vfs_seek(fs_handle h, s64 off, int whence);

static inline void vfs_retain_inode(filesystem *fs, vfs_inode_ptr_t inode)
{
   fs->fsops->retain_inode(fs, inode);
}

static inline void vfs_release_inode(filesystem *fs, vfs_inode_ptr_t inode)
{
   fs->fsops->release_inode(fs, inode);
}

static inline void vfs_retain_inode_at(vfs_path *p)
{
   vfs_retain_inode(p->fs, p->fs_path.inode);
}

static inline void vfs_release_inode_at(vfs_path *p)
{
   vfs_release_inode(p->fs, p->fs_path.inode);
}

static ALWAYS_INLINE filesystem *get_fs(fs_handle h)
{
   ASSERT(h != NULL);
   return ((fs_handle_base *)h)->fs;
}

static ALWAYS_INLINE void
vfs_get_entry(filesystem *fs,
              vfs_inode_ptr_t inode,
              const char *name,
              ssize_t name_len,
              fs_path_struct *fs_path)
{
   fs->fsops->get_entry(fs, inode, name, name_len, fs_path);
}

static ALWAYS_INLINE void
vfs_get_root_entry(filesystem *fs, fs_path_struct *fs_path)
{
   vfs_get_entry(fs, NULL, NULL, 0, fs_path);
}

/* Per-file locks */
void vfs_exlock(fs_handle h);
void vfs_exunlock(fs_handle h);
void vfs_shlock(fs_handle h);
void vfs_shunlock(fs_handle h);

/* Whole-filesystem locks */
void vfs_fs_exlock(filesystem *fs);
void vfs_fs_exunlock(filesystem *fs);
void vfs_fs_shlock(filesystem *fs);
void vfs_fs_shunlock(filesystem *fs);
/* --- */

int
compute_abs_path(const char *path, const char *str_cwd, char *dest, u32 dest_s);

u32 vfs_get_new_device_id(void);
fs_handle get_fs_handle(int fd);
void close_cloexec_handles(process_info *pi);

/* ------------ Current mount point interface ------------- */

typedef struct {

   filesystem *fs;
   u32 path_len;
   char path[0];

} mountpoint;

int mountpoint_add(filesystem *fs, const char *path);
void mountpoint_remove(filesystem *fs);
u32 mp_check_match(const char *mp, u32 lm, const char *path, u32 lp);

/* ------------ NEW mount point interface ------------- */

/*
 * Resolves `path` and returns in `rp` the corresponding VFS path with the
 * filesystem retained and locked, in case of success (return 0).
 *
 * In case of failure, it returns a value < 0 and the it does *not* require
 * any further clean-up.
 */
int
vfs_resolve(const char *path,
            vfs_path *rp,
            bool exlock,
            bool res_last_sl);

int mp2_init(filesystem *root_fs);
int mp2_add(filesystem *fs, const char *target_path);
int mp2_remove(const char *target_path);
filesystem *mp2_get_retained_at(filesystem *host_fs, vfs_inode_ptr_t inode);
filesystem *mp2_get_root(void);
