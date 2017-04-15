
#pragma once
#include <common_defs.h>

#define MAX_TASKLETS 1024

typedef struct {

   uptr arg1;
   uptr arg2;
   uptr arg3;

} tasklet_context;


void initialize_tasklets();

bool add_tasklet(void *func, void *arg1, void *arg2, void *arg3);
bool run_one_tasklet();
