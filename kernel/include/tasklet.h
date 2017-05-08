
#pragma once
#include <common_defs.h>

#define MAX_TASKLETS 1024

typedef struct {

   uptr arg1;
   uptr arg2;
   uptr arg3;

} tasklet_context;


void initialize_tasklets();

bool add_tasklet_int(void *func, uptr arg1, uptr arg2, uptr arg3);
bool run_one_tasklet(void);
void tasklet_runner_kthread();


#define add_tasklet3(f, a1, a2, a3) \
   add_tasklet_int((void *)(f), (uptr)(a1), (uptr)(a2), (uptr)(a3))

#define add_tasklet2(f, a1, a2) \
   add_tasklet_int((void *)(f), (uptr)(a1), (uptr)(a2), 0)

#define add_tasklet1(f, a1) \
   add_tasklet_int((void *)(f), (uptr)(a1), 0, 0)

#define add_tasklet0(f) \
   add_tasklet_int((void *)(f), 0, 0, 0)

