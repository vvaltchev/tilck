
#pragma once
#include <common/basic_defs.h>
#include <exos/process.h>

#define MAX_TASKLETS 1000

extern task_info *__tasklet_runner_task;

static inline task_info *get_tasklet_runner(void)
{
   return __tasklet_runner_task;
}

static inline bool is_tasklet(task_info *ti)
{
   return ti == __tasklet_runner_task;
}

typedef struct {

   uptr arg1;
   uptr arg2;

} tasklet_context;


void init_tasklets();

NODISCARD bool enqueue_tasklet_int(void *func, uptr arg1, uptr arg2);
bool run_one_tasklet(void);
void tasklet_runner_kthread(void);
bool any_tasklets_to_run(void);

#define enqueue_tasklet2(f, a1, a2) \
   enqueue_tasklet_int((void *)(f), (uptr)(a1), (uptr)(a2))

#define enqueue_tasklet1(f, a1) \
   enqueue_tasklet_int((void *)(f), (uptr)(a1), 0)

#define enqueue_tasklet0(f) \
   enqueue_tasklet_int((void *)(f), 0, 0)

