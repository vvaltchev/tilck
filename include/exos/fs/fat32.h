
#pragma once

#include <common/basic_defs.h>
#include <common/fat32_base.h>

#include <exos/sync.h>
#include <exos/fs/exvfs.h>

typedef struct {

   fat_header *hdr; /* vaddr of the beginning of the FAT partition */
   fat_type type;
   ssize_t cluster_size;

   kmutex ex_mutex; // big exclusive whole-filesystem lock
                    // TODO: use a rw-lock when available in the kernel

} fat_fs_device_data;

typedef struct {

   /* fs_handle_base */
   filesystem *fs;
   file_ops fops;

   /* fs-specific members */
   fat_entry *e;
   u32 pos;
   u32 curr_cluster;

} fat_file_handle;

filesystem *fat_mount_ramdisk(void *vaddr);
void fat_umount_ramdisk(filesystem *fs);
