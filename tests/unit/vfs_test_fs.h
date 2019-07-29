/* SPDX-License-Identifier: BSD-2-Clause */

#include <iostream>
#include <map>
#include "vfs_test.h"

using namespace std;

struct tfs_entry {

   const char *name;
   vfs_entry_type type;
   const char *symlink;
   tfs_entry *parent;
   int ref_count;
   map<string, tfs_entry *> children;

   tfs_entry *set_parents() {

      for (auto &p : children) {
         p.second->parent = this;
         p.second->set_parents();
      }

      return this;
   }

   void reset_refcounts() {
      for (auto &p : children) {
         p.second->ref_count = 0;
         p.second->reset_refcounts();
      }
   }
};


#define _NODE(n, t, s, ...) new tfs_entry{n, t, s, 0, 0, {__VA_ARGS__}}
#define N_FILE(name) make_pair(name, _NODE(name, VFS_FILE, 0))
#define N_SYM(name, s) make_pair(name, _NODE(name, VFS_SYMLINK, s))
#define N_DIR(name, ...) make_pair(name, _NODE(name, VFS_DIR, 0, __VA_ARGS__))
#define ROOT_NODE(...) (_NODE("", VFS_DIR, 0, __VA_ARGS__))->set_parents()

extern const fs_ops static_fsops_testfs;

void test_fs_register_mp(tfs_entry *where, tfs_entry *target);
void test_fs_clear_mps();
bool test_fs_is_mountpoint(tfs_entry *e);
void test_fs_check_refcounts(tfs_entry *node);

inline filesystem create_test_fs(const char *name, tfs_entry *root)
{
   filesystem fs {

      1,                        /* ref-count */
      name,                     /* fs type name */
      0,                        /* device_id */
      0,                        /* flags */
      root,                     /* device_data */
      &static_fsops_testfs,     /* fsops */
   };

   return fs;
}

inline tfs_entry *
path(tfs_entry *e, initializer_list<const char *> comps)
{
   for (auto comp_name : comps) {

      auto it = e->children.find(comp_name);

      if (it == e->children.end())
         return nullptr;

      e = it->second;
   }

   return e;
}
