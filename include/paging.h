
#pragma once

#include <commonDefs.h>


typedef struct {

   uint32_t present : 1;
   uint32_t rw : 1;        // read only = 0, read/write = 1
   uint32_t us :  1;       // user/supervisor
   uint32_t wt : 1;        // write-through
   uint32_t cd : 1;        // cache-disabled
   uint32_t accessed : 1;
   uint32_t zero : 1;
   uint32_t psize : 1;    // page size; 0 = 4 KB, 1 = 4 MB
   uint32_t ignored : 1;
   uint32_t avail : 3;
   uint32_t pageTableAddr : 20;

} page_directory_t;

typedef struct {

   uint32_t present : 1;
   uint32_t rw : 1;        // read only = 0, read/write = 1
   uint32_t us :  1;       // user/supervisor
   uint32_t wt : 1;        // write-through
   uint32_t cd : 1;        // cache-disabled
   uint32_t accessed : 1;
   uint32_t dirty : 1;
   uint32_t zero : 1;
   uint32_t global : 1;
   uint32_t avail : 3;
   uint32_t pageAddr : 20;

} page_table_t;
