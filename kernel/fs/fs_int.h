/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/kernel/fs/vfs.h>

typedef struct {

   filesystem *host_fs;
   vfs_inode_ptr_t host_fs_inode;
   filesystem *target_fs;

} mountpoint2;

typedef struct {

   const char *orig_path;       /* original path (used for offsets) */
   vfs_path paths[4];           /* paths stack */
   int ss;                      /* stack size */
   bool exlock;                 /* true -> use exlock, false -> use shlock */

} vfs_resolve_int_ctx;
