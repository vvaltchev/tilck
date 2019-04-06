/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/rwlock.h>
#include <tilck/kernel/sched.h>

void rwlock_rp_init(rwlock_rp *r)
{
   kmutex_init(&r->readers_lock, 0);
   ksem_init(&r->writers_sem);
   r->readers_count = 0;
}

void rwlock_rp_destroy(rwlock_rp *r)
{
   r->readers_count = 0;
   ksem_destroy(&r->writers_sem);
   kmutex_destroy(&r->readers_lock);
}

void rwlock_rp_shlock(rwlock_rp *r)
{
   kmutex_lock(&r->readers_lock);
   {
      if (++r->readers_count == 1)
         ksem_wait(&r->writers_sem);
   }
   kmutex_unlock(&r->readers_lock);
}

void rwlock_rp_shunlock(rwlock_rp *r)
{
   kmutex_lock(&r->readers_lock);
   {
      if (--r->readers_count == 0)
         ksem_signal(&r->writers_sem);
   }
   kmutex_unlock(&r->readers_lock);
}

void rwlock_rp_exlock(rwlock_rp *r)
{
   ksem_wait(&r->writers_sem);
}

void rwlock_rp_exunlock(rwlock_rp *r)
{
   ksem_signal(&r->writers_sem);
}
