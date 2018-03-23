
#pragma once
#include <basic_defs.h>

#define MAX_TASKLETS 1000

typedef struct {

   uptr arg1;
   uptr arg2;
   uptr arg3;

} tasklet_context;


void initialize_tasklets();

bool enqueue_tasklet_int(void *func, uptr arg1, uptr arg2, uptr arg3);
bool run_one_tasklet(void);
void tasklet_runner_kthread();


#define enqueue_tasklet3(f, a1, a2, a3) \
   enqueue_tasklet_int((void *)(f), (uptr)(a1), (uptr)(a2), (uptr)(a3))

#define enqueue_tasklet2(f, a1, a2) \
   enqueue_tasklet_int((void *)(f), (uptr)(a1), (uptr)(a2), 0)

#define enqueue_tasklet1(f, a1) \
   enqueue_tasklet_int((void *)(f), (uptr)(a1), 0, 0)

#define enqueue_tasklet0(f) \
   enqueue_tasklet_int((void *)(f), 0, 0, 0)

