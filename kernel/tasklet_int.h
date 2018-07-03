
#pragma once
#include <exos/ringbuf.h>

typedef void (*tasklet_func)(uptr, uptr);

typedef struct {

   tasklet_func fptr;
   tasklet_context ctx;

} tasklet;

typedef struct {

   tasklet *all_tasklets;
   ringbuf tasklet_ringbuf;
   kcond tasklet_cond;
   task_info *task;
   int priority; /* 0 => max priority */
   u32 limit;

} tasklet_thread_info;

extern tasklet_thread_info *tasklet_threads[MAX_TASKLET_THREADS];

bool run_one_tasklet(int tn);
