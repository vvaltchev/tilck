
#pragma once
#include <common/basic_defs.h>

#define MAX_TASKLETS 1000

/*
 * Because of the number of bits allocated in the implementation, MAX_TASKLETS
 * cannot be more than 1000 on 32-bit systems.
 */
STATIC_ASSERT(MAX_TASKLETS <= 1000);

typedef struct {

   uptr arg1;
   uptr arg2;

} tasklet_context;


void init_tasklets();

NODISCARD bool enqueue_tasklet_int(void *func, uptr arg1, uptr arg2);
bool run_one_tasklet(void);
void tasklet_runner_kthread(void);

#define enqueue_tasklet2(f, a1, a2) \
   enqueue_tasklet_int((void *)(f), (uptr)(a1), (uptr)(a2))

#define enqueue_tasklet1(f, a1) \
   enqueue_tasklet_int((void *)(f), (uptr)(a1), 0)

#define enqueue_tasklet0(f) \
   enqueue_tasklet_int((void *)(f), 0, 0)

