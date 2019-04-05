/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/kernel/rwlock.h>
#include <tilck/kernel/sched.h>

void rwlock_rp_init(rwlock_rp *r)
{
   ASSERT(!is_preemption_enabled());

   kmutex_init(&r->readers_lock, 0);
   kmutex_init(&r->writers_lock, 0);
   r->readers_count = 0;
}

void rwlock_rp_destroy(rwlock_rp *r)
{
   ASSERT(!is_preemption_enabled());

   r->readers_count = 0;
   kmutex_destroy(&r->writers_lock);
   kmutex_destroy(&r->readers_lock);
}

void rwlock_rp_shlock(rwlock_rp *r)
{
   kmutex_lock(&r->readers_lock);
   {
      if (++r->readers_count == 1)
         kmutex_lock(&r->writers_lock);
   }
   kmutex_unlock(&r->readers_lock);
}

void rwlock_rp_shunlock(rwlock_rp *r)
{
   kmutex_lock(&r->readers_lock);
   {
      if (--r->readers_count == 0)
         kmutex_unlock(&r->writers_lock);
   }
   kmutex_unlock(&r->readers_lock);
}

void rwlock_rp_exlock(rwlock_rp *r)
{
   kmutex_lock(&r->writers_lock);
}

void rwlock_rp_exunlock(rwlock_rp *r)
{
   kmutex_unlock(&r->writers_lock);
}
