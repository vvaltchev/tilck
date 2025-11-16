/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <stdbool.h>
#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/common/printk.h>
#include <tilck/common/utils.h>

#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/fs/flock.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/rwlock.h>
#include <tilck/kernel/datetime.h>
#include <tilck/kernel/bintree.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/process.h>

#include <tilck/kernel/test/vfs.h>

#include <sys/mman.h>      // system header
#include <dirent.h>        // system header

struct ramfs_inode;

struct ramfs_block {

   struct bintree_node node;
   offt offset;                  /* MUST BE divisible by PAGE_SIZE */
   void *vaddr;
};

/*
 * Ramfs entries do not *necessarily* need to have a fixed size, as they are
 * allocated dynamically on the heap. Said that, a fixed-size entry struct is
 * simpler to manage and faster to alloc/free, in particular with Tilck's
 * current kmalloc implementation.
 */
#define RAMFS_ENTRY_SIZE 256
#define RAMFS_ENTRY_MAX_LEN (                   \
   RAMFS_ENTRY_SIZE                             \
   - sizeof(struct bintree_node)                \
   - sizeof(struct list_node)                   \
   - sizeof(struct ramfs_inode *)               \
   - sizeof(u8)                                 \
)

struct ramfs_entry {

   struct bintree_node node;
   struct list_node lnode;
   struct ramfs_inode *inode;
   u8 name_len;                     /* NOTE: includes the final \0 */
   char name[RAMFS_ENTRY_MAX_LEN];
};

STATIC_ASSERT(sizeof(struct ramfs_entry) == RAMFS_ENTRY_SIZE);

struct ramfs_inode {

   /*
    * Inode's ref-count is the number of file handles currently pointing to
    * this inode.
    */
   REF_COUNTED_OBJECT;

   tilck_ino_t ino;
   enum vfs_entry_type type;
   struct rwlock_wp rwlock;
   nlink_t nlink;
   mode_t mode;
   size_t blocks_count;                /* count of page-size blocks */
   struct ramfs_inode *parent_dir;
   struct list mappings_list;          /* see ramfs_unmap_past_eof_mappings() */

   union {

      /* valid when type == VFS_FILE */
      struct {
         offt fsize;
         struct ramfs_block *blocks_tree_root;
      };

      /* valid when type == VFS_DIR */
      struct {
         offt num_entries;
         struct ramfs_entry *entries_tree_root;
         struct list entries_list;
         struct list handles_list;
      };

      /* valid when type == VFS_SYMLINK */
      struct {
         size_t path_len;
         char *path;
      };
   };

   /* TODO: consider introducing `atime`      */
   struct k_timespec64 mtime;
   struct k_timespec64 ctime;
};

struct ramfs_handle {

   /* struct fs_handle_base */
   FS_HANDLE_BASE_FIELDS

   /* ramfs-specific fields */
   struct ramfs_inode *inode;

   /* valid only if inode->type == VFS_DIR */
   struct {
      struct list_node node;        /* node in inode->handles_list */
      struct ramfs_entry *dpos;     /* current entry position */
   };
};

STATIC_ASSERT(sizeof(struct ramfs_handle) <= MAX_FS_HANDLE_SIZE);

struct ramfs_data {

   struct rwlock_wp rwlock;

   tilck_ino_t next_inode_num;
   struct ramfs_inode *root;
};

CREATE_FS_PATH_STRUCT(ramfs_path, struct ramfs_inode *, struct ramfs_entry *);
/* ------------------------------------------------------------------------- */

/* ramfs ops */
static ssize_t ramfs_read(fs_handle h, char *buf, size_t len, offt *pos);
static ssize_t ramfs_write(fs_handle h, char *buf, size_t len, offt *pos);
static ssize_t ramfs_readv(fs_handle h, const struct iovec *iov, int iovcnt);
static ssize_t ramfs_writev(fs_handle h, const struct iovec *iov, int iovcnt);
static offt ramfs_seek(fs_handle h, offt off, int whence);
static int ramfs_ioctl(fs_handle h, ulong cmd, void *argp);
static int ramfs_mmap(struct user_mapping *um, pdir_t *pdir, int flags);
static int ramfs_munmap(struct user_mapping *um, void *vaddrp, size_t len);
static bool
ramfs_handle_fault(struct user_mapping *um, void *vaddrp, bool p, bool rw);

/* internal funcs */
static void ramfs_file_exlock(fs_handle h);
static void ramfs_file_exunlock(fs_handle h);
static void ramfs_file_shlock(fs_handle h);
static void ramfs_file_shunlock(fs_handle h);
static void ramfs_exlock(struct mnt_fs *fs);
static void ramfs_exunlock(struct mnt_fs *fs);
static void ramfs_shlock(struct mnt_fs *fs);
static void ramfs_shunlock(struct mnt_fs *fs);

static int
ramfs_inode_extend(struct ramfs_inode *i, offt new_len);

static int
ramfs_inode_truncate_safe(struct ramfs_inode *i, offt len, bool no_perm_check);

static struct ramfs_inode *
ramfs_create_inode_file(struct ramfs_data *d,
                        mode_t mode,
                        struct ramfs_inode *parent);

static int
ramfs_destroy_inode(struct ramfs_data *d, struct ramfs_inode *i);

static int
ramfs_dir_add_entry(struct ramfs_inode *idir,
                    const char *iname,
                    struct ramfs_inode *ie);

static void
ramfs_dir_remove_entry(struct ramfs_inode *idir, struct ramfs_entry *e);

static struct ramfs_inode *
ramfs_create_inode_dir(struct ramfs_data *d,
                       mode_t mode,
                       struct ramfs_inode *parent);

static struct ramfs_block *
ramfs_new_block(offt page);

static void
ramfs_destroy_block(struct ramfs_block *b);

static void
ramfs_append_new_block(struct ramfs_inode *inode, struct ramfs_block *block);


