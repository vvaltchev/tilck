
#pragma once

#include <common_defs.h>
#include <process.h>

#define WOBJ_NONE   0
#define WOBJ_KMUTEX 1

typedef struct {

   int type;
   void *ptr;

} wait_obj;

static inline void wait_obj_set(wait_obj *obj, int type, void *ptr)
{
   obj->type = type;
   obj->ptr = ptr;
}

static inline void wait_obj_reset(wait_obj *obj)
{
   obj->type = WOBJ_NONE;
   obj->ptr = NULL;
}

typedef struct {

   uptr id;
   task_info *owner_task;

} kmutex;


void kmutex_init(kmutex *m);
void lock(kmutex *m);
void unlock(kmutex *m);
void kmutex_destroy(kmutex *m);


