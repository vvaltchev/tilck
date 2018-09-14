/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/pq.h>
#include <tilck/kernel/errno.h>

#define HALF(x) ((x) >> 1)
#define TWICE(x) ((x) << 1)
#define LEFT(n) (TWICE(n) + 1)
#define RIGHT(n) (TWICE(n) + 2)
#define PARENT(n) (HALF(n-1))

struct pqueue_elem {
   void *data;
   int priority;
};

int pqueue_init(pqueue *pq, size_t capacity)
{
   pq->size = 0;
   pq->capacity = capacity;
   pq->elems = kmalloc(capacity * sizeof(pqueue_elem));

   if (!pq->elems)
      return -ENOMEM;

   return 0;
}

void pqueue_destroy(pqueue *pq)
{
   kfree2(pq->elems, pq->capacity);
   bzero(pq, sizeof(pqueue_elem));
}

static inline void pq_swap(pqueue *pq, int i, int j)
{
   pqueue_elem tmp = pq->elems[i];
   pq->elems[i] = pq->elems[j];
   pq->elems[j] = tmp;
}

void pqueue_push(pqueue *pq, void *data, int p)
{
   ASSERT(pq->size < pq->capacity);

   pq->elems[pq->size].data = data;
   pq->elems[pq->size].priority = p;
   pq->size++;

   if (pq->size <= 1)
      return;

   // heap-up operation
   for (int i = pq->size - 1;
        pq->elems[i].priority < pq->elems[PARENT(i)].priority; i = PARENT(i))
   {
      pq_swap(pq, i, PARENT(i));
   }
}


static void heap_down(pqueue *pq)
{
   int i = 0;
   int len = pq->size;
   pqueue_elem *heap = pq->elems;

   if (len <= 1)
      return;

   while (LEFT(i) < len) {

      int e = heap[i].priority;
      int l = heap[LEFT(i)].priority;

      if (RIGHT(i) < len) {

         int r = heap[RIGHT(i)].priority;

         if (e < l && e < r)
            break;

         /* index of the smallest child */
         int si = l < r ? LEFT(i) : RIGHT(i);

         pq_swap(pq, i, si);
         i = si;

      } else if (l < e) {

         pq_swap(pq, i, LEFT(i));
         i = LEFT(i);

      } else {
         break;
      }
   }
}


void *pqueue_pop(pqueue *pq)
{
   ASSERT(pq->size > 0);
   void *res = pq->elems[0].data;

   pq_swap(pq, 0, pq->size - 1);
   pq->size--;

   heap_down(pq);
   return res;
}
