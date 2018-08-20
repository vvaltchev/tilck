
#include <tilck/common/basic_defs.h>

typedef struct pqueue_elem pqueue_elem;
typedef struct pqueue pqueue;

struct pqueue {

   size_t size;
   size_t capacity;
   pqueue_elem *elems;
};

int pqueue_init(pqueue *pq, size_t capacity);
void pqueue_destroy(pqueue *pq);

void pqueue_push(pqueue *pq, void *data, int priority);
void *pqueue_pop(pqueue *pq);

static inline bool pqueue_is_empty(pqueue *pq)
{
   return pq->size == 0;
}

static inline bool pqueue_is_full(pqueue *pq)
{
   return pq->size == pq->capacity;
}
