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

struct process;
struct user_mapping;
struct fs;

/*
 * Opaque type for file handles.
 *
 * The only requirement for such handles is that they must have at their
 * beginning all the members of fs_handle_base. Therefore, a fs_handle MUST
 * always be castable to fs_handle_base *.
 */
typedef void *fs_handle;

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
 * It is the primary member the struct fs_path and it's used by functions like
 * (stat, fstat), (truncate, ftruncate) in order to have a common implementation
 * in the FS layer.
 */
typedef void *vfs_inode_ptr_t;

#define CREATE_FS_PATH_STRUCT(name, inode_type, fs_entry_type)            \
                                                                          \
   STATIC_ASSERT(sizeof(inode_type) == sizeof(vfs_inode_ptr_t));          \
   STATIC_ASSERT(sizeof(fs_entry_type) == sizeof(void *));                \
                                                                          \
   struct name {                                                          \
      inode_type inode;                                                   \
      inode_type dir_inode;                                               \
      fs_entry_type dir_entry;                                            \
      enum vfs_entry_type type;                                           \
   }                                                                      \

CREATE_FS_PATH_STRUCT(fs_path, vfs_inode_ptr_t, void *);

struct vfs_path {

   struct fs *fs;
   struct fs_path fs_path;

   /* other fields */
   const char *last_comp;
};

struct vfs_dent64 {

   tilck_ino_t ino;
   enum vfs_entry_type type;
   u8 name_len;               /* NODE: includes the final '\0' */
   const char *name;
};

typedef int (*get_dents_func_cb) (struct vfs_dent64 *, void *);

/* fs ops */
typedef vfs_inode_ptr_t (*func_get_inode) (fs_handle);

typedef void    (*func_close)     (fs_handle);
typedef int     (*func_open)      (struct vfs_path *, fs_handle *, int, mode_t);
typedef int     (*func_dup)       (fs_handle, fs_handle *);
typedef int     (*func_getdents)  (fs_handle, get_dents_func_cb, void *);
typedef int     (*func_unlink)    (struct vfs_path *p);
typedef int     (*func_mkdir)     (struct vfs_path *p, mode_t);
typedef int     (*func_rmdir)     (struct vfs_path *p);
typedef int     (*func_symlink)   (const char *, struct vfs_path *);
typedef int     (*func_readlink)  (struct vfs_path *, char *);
typedef int     (*func_chmod)     (struct fs *, vfs_inode_ptr_t, mode_t);
typedef void    (*func_fslock_t)  (struct fs *);
typedef int     (*func_rr_inode)  (struct fs *, vfs_inode_ptr_t);

typedef int     (*func_futimens)  (struct fs *,
                                   vfs_inode_ptr_t,
                                   const struct k_timespec64 times[2]);

typedef int     (*func_2paths)    (struct fs *,
                                   struct vfs_path *,
                                   struct vfs_path *);

typedef func_2paths func_rename;
typedef func_2paths func_link;

typedef void    (*func_get_entry) (struct fs *fs,
                                   void *dir_inode,
                                   const char *name,
                                   ssize_t name_len,
                                   struct fs_path *fs_path);

/* mixed fs/file ops */
typedef int     (*func_stat)   (struct fs *, vfs_inode_ptr_t, struct stat64 *);
typedef int     (*func_trunc)  (struct fs *, vfs_inode_ptr_t, offt);

/* file ops */
typedef ssize_t        (*func_read)         (fs_handle, char *, size_t);
typedef ssize_t        (*func_write)        (fs_handle, char *, size_t);
typedef offt           (*func_seek)         (fs_handle, offt, int);
typedef int            (*func_ioctl)        (fs_handle, ulong, void *);
typedef int            (*func_mmap)         (struct user_mapping *, bool);
typedef int            (*func_munmap)       (fs_handle, void *, size_t);
typedef bool           (*func_handle_fault) (fs_handle, void *, bool, bool);
typedef int            (*func_rwe_ready)    (fs_handle);
typedef struct kcond  *(*func_get_rwe_cond) (fs_handle);

typedef ssize_t        (*func_readv)        (fs_handle,
                                             const struct iovec *,
                                             int);

typedef ssize_t        (*func_writev)       (fs_handle,
                                             const struct iovec *,
                                             int);


#define VFS_FS_RO                  (0)  /* struct fs mounted in RO mode */
#define VFS_FS_RW             (1 << 0)  /* struct fs mounted in RW mode */
#define VFS_FS_RQ_DE_SKIP     (1 << 1)  /* FS requires vfs dents skip */

/*
 * Operations affecting the file system structure (directories, files, etc.).
 *
 * What are the fs-lock functions
 * ---------------------------------
 *
 * The four fs-lock funcs below are supposed to be implemented by each
 * struct fs in order to protect its tree structure from races, typically by
 * using a read-write lock under the hood. Yes, that means that for example two
 * creat() operations even in separate directories cannot happen at the same
 * time, on the same FS. But, given that Tilck does NOT support SMP, this
 * approach not only offers a great simplification, but it actually increases
 * the overall throughput of the system (fine-grain per-directory locking is
 * pretty expensive).
 */
struct fs_ops {

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
   func_futimens futimens;
   func_rr_inode retain_inode;
   func_rr_inode release_inode;

   /* file system structure lock funcs */
   func_fslock_t fs_exlock;
   func_fslock_t fs_exunlock;
   func_fslock_t fs_shlock;
   func_fslock_t fs_shunlock;
};

/* This struct is Tilck's analogue of Linux's "superblock" */
struct fs {

   REF_COUNTED_OBJECT;

   const char *fs_type_name; /* statically allocated: do NOT free() */
   u32 device_id;
   u32 flags;
   void *device_data;
   const struct fs_ops *fsops;
};

struct file_ops {

   /* Main funcs, all optional */
   func_read read;                     /* if NULL -> -EBADF  */
   func_write write;                   /* if NULL -> -EBADF  */
   func_ioctl ioctl;                   /* if NULL -> -ENOTTY */
   func_seek seek;                     /* if NULL -> -ESPIPE */
   func_mmap mmap;                     /* if NULL -> -ENODEV */
   func_munmap munmap;                 /* if NULL -> -ENODEV */

   func_readv readv;                   /* if NULL, emulated in non-atomic way */
   func_writev writev;                 /* if NULL, emulated in non-atomic way */

   func_handle_fault handle_fault;

   /*
    * Optional, r/w/e ready funcs
    *
    * NOTE[1]: implementing at least the read and write ones is essential in
    * order to support select() and poll().
    *
    * NOTE[2]: `except` here stands for `unfetched exceptional condition`.
    */
   func_rwe_ready read_ready;          /* if NULL, return true */
   func_rwe_ready write_ready;         /* if NULL, return true */
   func_rwe_ready except_ready;        /* if NULL, return true */

   func_get_rwe_cond get_rready_cond;  /* if NULL, return NULL */
   func_get_rwe_cond get_wready_cond;  /* if NULL, return NULL */
   func_get_rwe_cond get_except_cond;  /* if NULL, return NULL */
};

/*
 * Each fs_handle struct should contain at its beginning the fields of the
 * following base struct [a rough attempt to emulate inheritance in C].
 *
 * TODO: introduce a ref-count in the fs_base_handle struct when implementing
 * thread support.
 */

#define FS_HANDLE_BASE_FIELDS    \
   struct process *pi;           \
   struct fs *fs;                \
   const struct file_ops *fops;  \
   int fd_flags;                 \
   int fl_flags;                 \
   int spec_flags;               \
   offt pos;                        /* file: offset, dir: opaque entry index */

struct fs_handle_base {
   FS_HANDLE_BASE_FIELDS
};

/* File handle's special flags (spec_flags) */
#define VFS_SPFL_NO_USER_COPY         (1 << 0)
#define VFS_SPFL_MMAP_SUPPORTED       (1 << 1)
/* --- */

void vfs_init_fs_handle_base_fields(struct fs_handle_base *hb,
                                    struct fs *fs,
                                    const struct file_ops *fops);

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
int vfs_utimens(const char *path, const struct k_timespec64 times[2]);

int vfs_ftruncate(fs_handle h, offt length);
int vfs_ioctl(fs_handle h, ulong request, void *argp);
int vfs_fstat64(fs_handle h, struct stat64 *statbuf);
int vfs_dup(fs_handle h, fs_handle *dup_h);
int vfs_getdents64(fs_handle h, struct linux_dirent64 *dirp, u32 bs);
int vfs_mmap(struct user_mapping *um, bool register_only);
int vfs_munmap(fs_handle h, void *vaddr, size_t len);
int vfs_fchmod(fs_handle h, mode_t mode);
void vfs_close(fs_handle h);
void vfs_close2(struct process *pi, fs_handle h);
bool vfs_handle_fault(fs_handle h, void *va, bool p, bool rw);
int vfs_futimens(fs_handle h, const struct k_timespec64 times[2]);


int vfs_read_ready(fs_handle h);
int vfs_write_ready(fs_handle h);
int vfs_except_ready(fs_handle h);
struct kcond *vfs_get_rready_cond(fs_handle h);
struct kcond *vfs_get_wready_cond(fs_handle h);
struct kcond *vfs_get_except_cond(fs_handle h);

ssize_t vfs_read(fs_handle h, void *buf, size_t buf_size);
ssize_t vfs_write(fs_handle h, void *buf, size_t buf_size);
ssize_t vfs_readv(fs_handle h, const struct iovec *iov, int iovcnt);
ssize_t vfs_writev(fs_handle h, const struct iovec *iov, int iovcnt);

offt vfs_seek(fs_handle h, s64 off, int whence);

static ALWAYS_INLINE bool
is_mmap_supported(fs_handle h)
{
   struct fs_handle_base *hb = (struct fs_handle_base *)h;
   return !!(hb->spec_flags & VFS_SPFL_MMAP_SUPPORTED);
}

static inline void vfs_retain_inode(struct fs *fs, vfs_inode_ptr_t inode)
{
   fs->fsops->retain_inode(fs, inode);
}

static inline void vfs_release_inode(struct fs *fs, vfs_inode_ptr_t inode)
{
   fs->fsops->release_inode(fs, inode);
}

static inline void vfs_retain_inode_at(struct vfs_path *p)
{
   vfs_retain_inode(p->fs, p->fs_path.inode);
}

static inline void vfs_release_inode_at(struct vfs_path *p)
{
   vfs_release_inode(p->fs, p->fs_path.inode);
}

static ALWAYS_INLINE struct fs *get_fs(fs_handle h)
{
   ASSERT(h != NULL);
   return ((struct fs_handle_base *)h)->fs;
}

static ALWAYS_INLINE void
vfs_get_entry(struct fs *fs,
              vfs_inode_ptr_t inode,
              const char *name,
              ssize_t name_len,
              struct fs_path *fs_path)
{
   fs->fsops->get_entry(fs, inode, name, name_len, fs_path);
}

static ALWAYS_INLINE void
vfs_get_root_entry(struct fs *fs, struct fs_path *fs_path)
{
   vfs_get_entry(fs, NULL, NULL, 0, fs_path);
}

/* Whole-struct fs locks */
void vfs_fs_exlock(struct fs *fs);
void vfs_fs_exunlock(struct fs *fs);
void vfs_fs_shlock(struct fs *fs);
void vfs_fs_shunlock(struct fs *fs);
/* --- */

int
compute_abs_path(const char *path, const char *str_cwd, char *dest, u32 dest_s);

u32 vfs_get_new_device_id(void);
fs_handle get_fs_handle(int fd);
void close_cloexec_handles(struct process *pi);

/* ------------ Current mount point interface ------------- */

/*
 * Resolves `path` and returns in `rp` the corresponding VFS path with the
 * struct fs retained and locked, in case of success (return 0).
 *
 * In case of failure, it returns a value < 0 and the it does *not* require
 * any further clean-up.
 */
int
vfs_resolve(const char *path,
            struct vfs_path *rp,
            bool exlock,
            bool res_last_sl);

int mp_init(struct fs *root_fs);
int mp_add(struct fs *fs, const char *target_path);
int mp_remove(const char *target_path);
struct fs *mp_get_retained_at(struct fs *host_fs, vfs_inode_ptr_t inode);
struct fs *mp_get_root(void);
