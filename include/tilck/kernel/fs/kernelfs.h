/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/kernel/fs/vfs.h>

#define KOBJ_BASE_FIELDS                    \
   REF_COUNTED_OBJECT;                      \
   void (*on_handle_close)(fs_handle h);    \
   void (*on_handle_dup)(fs_handle h);      \
   void (*destory_obj)(struct kobj_base *);

struct kobj_base {

   KOBJ_BASE_FIELDS
};

struct kfs_handle {

   FS_HANDLE_BASE_FIELDS
   struct kobj_base *kobj;
};

void init_kernelfs(void);
void kfs_destroy_handle(struct kfs_handle *h);
struct kfs_handle *
kfs_create_new_handle(const struct file_ops *fops,
                      struct kobj_base *kobj,
                      int fl_flags);
