/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/common/fat32_base.h>

#include <tilck/kernel/sync.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/datetime.h>

struct fat_fs_device_data {

   struct fat_hdr *hdr; /* vaddr of the beginning of the FAT partition */
   enum fat_type type;
   u32 cluster_size;
   u32 root_cluster;

   /*
    * A pointer to root directory's entries. Notice that this isn't a random
    * choice: the first entry in the root directory the is "Volume ID" entry,
    * which has some of the traits of regular dir entries, but it doesn't have
    * the cluster information (we cannot pass it to fat_get_first_cluster()),
    * so it still requires to be treated in a special way. That's the main
    * reason for calling this member `root_dir_entries` and not `root_entry`
    * as in the past: because it does not complain a 100% with the idea of a
    * regular fat_entry.
    */
   struct fat_entry *root_dir_entries;
};

struct fatfs_handle {

   /* struct fs_handle_base */
   FS_HANDLE_BASE_FIELDS

   /* fs-specific members */
   struct fat_entry *e;
   u32 curr_cluster;
};

struct fs *fat_mount_ramdisk(void *vaddr, u32 flags);
void fat_umount_ramdisk(struct fs *fs);

struct datetime
fat_datetime_to_regular_datetime(u16 date, u16 time, u8 timetenth);

/*
 * On FAT, there are no inodes and dir entries. Just dir entries.
 * Therefore, what is called `inode` in VFS will be a `entry` here.
 * The `dir_entry` instead will be unused.
 */

#define inode        entry
#define dir_inode    parent_entry
#define dir_entry    unused

CREATE_FS_PATH_STRUCT(fat_fs_path, struct fat_entry *, void *);

#undef inode
#undef dir_inode
#undef dir_entry
