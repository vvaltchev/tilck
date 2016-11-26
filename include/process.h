
#pragma once

#include <commonDefs.h>
#include <paging.h>

#ifdef __i386__
#define PROCESS_REGS_DATA_SIZE 76
#endif


struct process_info;
typedef struct process_info process_info;

struct process_info {

   uint8_t state_regs[PROCESS_REGS_DATA_SIZE];
   page_directory_t *pdir;

};


