
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/sort.h>

/*
 * Insertion sort for pointer-size "objects".
 */
void insertion_sort_ptr(uptr *arr, int elem_count, cmpfun_ptr cmp)
{
   for (int i = 1; i < elem_count; i++) {

      uptr elem = arr[i]; // save the element that has to be placed
      int j;              // 'j' is its destination

      for (j = i - 1; j >= 0 && cmp(&arr[j], &elem) > 0; j--) {
         arr[j+1] = arr[j]; // the first arr[j+1] was arr[i] = elem.
      }

      arr[j+1] = elem;
   }
}

/*
 * Generic insertion_sort implementation for objects of size 'elem_size'.
 */

void
insertion_sort_generic(void *a, uptr elem_size, int elem_count, cmpfun_ptr cmp)
{
   char elem[elem_size]; // VLA
   int j;

   for (int i = 1; i < elem_count; i++) {

      memcpy(&elem, a + i * elem_size, elem_size); /* elem = a[i] */

      for (j = i - 1; j >= 0 && cmp(a + j * elem_size, &elem) > 0; j--) {
         memcpy(a + (j + 1) * elem_size,
                a + j * elem_size,
                elem_size); /* a[j+1] = a[j] */
      }

      memcpy(a + (j + 1) * elem_size, &elem, elem_size); /* a[j+1] = elem */
   }
}
