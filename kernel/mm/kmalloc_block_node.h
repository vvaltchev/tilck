
#pragma once
#include <tilck/common/basic_defs.h>

#define FL_NODE_SPLIT      (1 << 0)
#define FL_NODE_FULL       (1 << 1)
#define FL_NODE_ALLOCATED  (1 << 2)

#define FL_NODE_SPLIT_AND_FULL      (FL_NODE_SPLIT | FL_NODE_FULL)
#define FL_NODE_ALLOCATED_AND_FULL  (FL_NODE_ALLOCATED | FL_NODE_FULL)
#define FL_NODE_ALLOCATED_AND_SPLIT (FL_NODE_ALLOCATED | FL_NODE_SPLIT)

typedef struct {

   union {

      struct {
         // 1 if the block has been split. Check its children.
         u8 split : 1;

         // 1 means obviously completely full
         // 0 means completely empty if split=0, or partially empty if split=1
         u8 full : 1;

         u8 allocated : 1;    // only for nodes with size = alloc_block_size
         u8 alloc_failed : 1; // only for nodes with size = alloc_block_size

         u8 unused : 4; // Free unused (for now) bits.
      };

      u8 raw;
   };

} block_node;
