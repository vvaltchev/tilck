/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/common/fat32_base.h>

#include <tilck/kernel/sync.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/datetime.h>

typedef struct {

   fat_header *hdr; /* vaddr of the beginning of the FAT partition */
   fat_type type;
   size_t cluster_size;

   fat_entry *root_entry;
   u32 root_cluster;

} fat_fs_device_data;

typedef struct {

   /* fs_handle_base */
   FS_HANDLE_BASE_FIELDS

   /* fs-specific members */
   fat_entry *e;
   u32 curr_cluster;

} fat_file_handle;

filesystem *fat_mount_ramdisk(void *vaddr, u32 flags);
void fat_umount_ramdisk(filesystem *fs);
datetime_t fat_datetime_to_regular_datetime(u16 date, u16 time, u8 timetenth);

/*
 * On FAT, there are no inodes and dir entries. Just dir entries.
 * Therefore, what is called `inode` in VFS will be a `entry` here.
 * The `dir_entry` instead will be unused.
 */

#define inode        entry
#define dir_inode    parent_entry
#define dir_entry    unused

CREATE_FS_PATH_STRUCT(fat_fs_path, fat_entry *, void *);

#undef inode
#undef dir_inode
#undef dir_entry
