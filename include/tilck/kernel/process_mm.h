/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/kernel/fs/vfs_base.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/list.h>

enum user_mapping_type {

   USER_MAPPING_NONE  = 0,
   USER_MAPPING_MMAP,      /* mmap()ed region; lives in the mmap_heap        */
   USER_MAPPING_PROG,      /* ELF PT_LOAD segment (text/rodata/data+bss)     */
   USER_MAPPING_STACK,     /* main-thread user stack                         */
   USER_MAPPING_HEAP,      /* brk() heap; its `len` tracks pi->brk           */
};

struct user_mapping {

   struct list_node pi_node;
   struct list_node inode_node;
   struct process *pi;

   fs_handle h;
   size_t len;
   size_t off;

   union {
      void *vaddrp;
      ulong vaddr;
   };

   int prot;
   enum user_mapping_type type;

};

struct user_mapping *
new_user_mapping(struct list *mappings,
                 enum user_mapping_type type,
                 struct process *pi,
                 fs_handle h,
                 void *v,
                 size_t ln,
                 size_t off,
                 int prot);
struct user_mapping *
process_add_user_mapping(fs_handle h, void *v, size_t ln, size_t off, int prot);
void process_remove_user_mapping(struct user_mapping *um);
void full_remove_user_mapping(struct process *pi, struct user_mapping *um);
void remove_all_mappings_of_handle(struct process *pi, fs_handle h);
void remove_all_user_mappings(struct process *pi);
struct user_mapping *process_get_user_mapping(void *vaddr);
void remove_all_file_mappings(struct process *pi);
struct mappings_info *
duplicate_mappings_info(struct process *new_pi, struct mappings_info *mi);
struct mappings_info *alloc_mappings_info(void);
void free_mappings_info(struct mappings_info *mi);


/* Internal functions */
bool user_valloc_and_map(ulong user_vaddr, size_t page_count);
void user_vfree_and_unmap(ulong user_vaddr, size_t page_count);
void user_unmap_zero_page(ulong user_vaddr, size_t page_count);
bool user_map_zero_page(ulong user_vaddr, size_t page_count);
int generic_fs_munmap(struct user_mapping *um, void *vaddrp, size_t len);

/* Special one-time funcs */
void set_kernel_process_pdir(pdir_t *pdir);
