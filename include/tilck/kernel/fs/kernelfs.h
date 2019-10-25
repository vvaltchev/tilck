/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/kernel/fs/vfs.h>

#define KOBJ_BASE_FIELDS                    \
   REF_COUNTED_OBJECT;                      \
   void (*destory)(struct kobj_base *);

struct kobj_base {

   KOBJ_BASE_FIELDS
};

struct kernel_fs_handle {

   FS_HANDLE_BASE_FIELDS
   struct kobj_base *kobj;
};

void init_kernelfs(void);
struct kernel_fs_handle *kfs_create_new_handle(void);
