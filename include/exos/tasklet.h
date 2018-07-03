
#pragma once
#include <common/basic_defs.h>
#include <exos/process.h>

#define MAX_TASKLET_THREADS 64

typedef struct {

   uptr arg1;
   uptr arg2;

} tasklet_context;

void init_tasklets();

task_info *get_tasklet_runner(int tn);
int create_tasklet_thread(int tn, int limit);
bool any_tasklets_to_run(int tn);
int get_tasklet_runner_limit(int tn);
void destroy_last_tasklet_thread(void);

task_info *get_highest_runnable_priority_tasklet_runner(void);

NODISCARD bool enqueue_tasklet_int(int tn, void *func, uptr arg1, uptr arg2);

#define enqueue_tasklet2(tn, f, a1, a2) \
   enqueue_tasklet_int(tn, (void *)(f), (uptr)(a1), (uptr)(a2))

#define enqueue_tasklet1(tn, f, a1) \
   enqueue_tasklet_int(tn, (void *)(f), (uptr)(a1), 0)

#define enqueue_tasklet0(tn, f) \
   enqueue_tasklet_int(tn, (void *)(f), 0, 0)

