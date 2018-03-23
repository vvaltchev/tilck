
#include <common/basic_defs.h>

#include <exos/kmalloc.h>
#include <exos/pq.h>

#define LEFT(i) (2*(i)+1)
#define RIGHT(i) (2*(i)+2)
#define PARENT(i) ((i - 1)/2)

struct pqueue_elem {

   void *data;
   int priority;
};

void pqueue_init(pqueue *pq, size_t capacity)
{
   pq->size = 0;
   pq->capacity = capacity;
   pq->elems = kmalloc(capacity * sizeof(pqueue_elem));
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
        pq->elems[i].priority < pq->elems[PARENT(i)].priority; i = PARENT(i)) {
      pq_swap(pq, i, PARENT(i));
   }
}


static void heap_down(pqueue *pq)
{
   pqueue_elem *heap = pq->elems;
   int len = pq->size;

   if (len <= 1) return;

   int i = 0;

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
