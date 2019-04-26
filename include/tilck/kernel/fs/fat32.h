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
   off_t pos;
   u32 curr_cluster;

} fat_file_handle;

filesystem *fat_mount_ramdisk(void *vaddr, u32 flags);
void fat_umount_ramdisk(filesystem *fs);
datetime_t fat_datetime_to_regular_datetime(u16 date, u16 time, u8 timetenth);

/*
 * On FAT, there are no inodes and dir entries. Just dir entries.
 * But, in order to walk a directory we generally need its first cluster,
 * except that on FAT-12 and FAT-16, there's no cluster# for the root dir
 * as it is NOT a cluster-chain. We have only a fat_entry* for its entries.
 * On FAT32 instead, we don't have a fat_entry*, but just a cluster number
 * and the root directory *is* a cluster-chain. Therefore, at least for the
 * parent dir, we need both a fat_entry* and cluster number to cover all the
 * cases.
 *
 * Since there in FAT there's no difference between `inode` and `entry`,
 * what is called `inode` in VFS will be a fat_entry* here. The dir_entry
 * field instead, will be used as parent dir cluster number.
 */

#define inode        entry
#define dir_inode    parent_entry
#define dir_entry    parent_cluster

CREATE_FS_PATH_STRUCT(fat_fs_path, fat_entry *, uptr);

#undef inode
#undef dir_inode
#undef dir_entry
