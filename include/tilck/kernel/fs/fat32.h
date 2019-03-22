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

   kmutex ex_mutex; // big exclusive whole-filesystem lock
                    // TODO: use a rw-lock when available in the kernel

} fat_fs_device_data;

typedef struct {

   /* fs_handle_base */
   filesystem *fs;
   file_ops fops;
   int flags;

   /* fs-specific members */
   fat_entry *e;
   u32 pos;
   u32 curr_cluster;
   u32 curr_file_index; /* used by fat_getdents64 */

} fat_file_handle;

filesystem *fat_mount_ramdisk(void *vaddr, u32 flags);
void fat_umount_ramdisk(filesystem *fs);
datetime_t fat_datetime_to_regular_datetime(u16 date, u16 time, u8 timetenth);
