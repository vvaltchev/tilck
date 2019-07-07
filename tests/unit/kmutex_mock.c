#include <tilck/kernel/sync.h>
#include <tilck/kernel/sched.h>

void __wrap_kmutex_lock(kmutex *m) {
   ASSERT(m->owner_task == NULL);
   m->owner_task = get_curr_task();
}

void __wrap_kmutex_unlock(kmutex *m) {
   ASSERT(m->owner_task == get_curr_task());
   m->owner_task = NULL;
}
