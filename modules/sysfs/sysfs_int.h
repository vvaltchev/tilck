/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/fs/vfs.h>
#include <tilck/mods/sysfs.h>
#include <tilck/mods/sysfs_utils.h>

struct sysfs_inode;

#define SYSFS_ENTRY_SIZE                       64
#define SYSFS_ENTRY_MAX_LEN (                   \
   SYSFS_ENTRY_SIZE                             \
   - sizeof(struct bintree_node)                \
   - sizeof(struct list_node)                   \
   - sizeof(struct ramfs_inode *)               \
   - sizeof(u8)                                 \
)

struct sysfs_entry {
   struct bintree_node node;
   struct list_node lnode;
   struct sysfs_inode *inode;
   u8 name_len;                     /* NOTE: includes the final \0 */
   char name[SYSFS_ENTRY_MAX_LEN];
};

STATIC_ASSERT(sizeof(struct sysfs_entry) == SYSFS_ENTRY_SIZE);

struct sysfs_inode {

   REF_COUNTED_OBJECT;

   enum vfs_entry_type type;
   tilck_ino_t ino;

   union {

      struct {

         struct sysobj *obj;
         struct sysobj_prop *prop;
         void *prop_data;

      } file;

      struct {

         offt num_entries;
         struct ramfs_entry *entries_tree_root;
         struct list entries_list;
         struct sysobj *obj;

      } dir;

      struct {

         u32 path_len;
         char *path;

      } symlink;
   };
};

struct sysfs_data {

   struct sysfs_inode *root;

   struct rwlock_wp rwlock;
   tilck_ino_t next_inode;
   time_t wrt_time;
   struct list dirty_handles;
};

struct sysfs_handle {

   /* struct fs_handle_base */
   FS_HANDLE_BASE_FIELDS

   enum vfs_entry_type type;
   struct sysfs_inode *inode;

   union {

      struct {

         char *data;
         offt data_len;
         offt data_max_len;
         struct list_node dirty_node;

      } file;

      struct {

         struct sysfs_entry *dpos;

      } dir;
   };
};

CREATE_FS_PATH_STRUCT(sysfs_path, struct sysfs_inode *, struct sysfs_entry *);

