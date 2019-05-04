/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>

#ifndef STACK_VAR
   #define STACK_VAR stack
#endif

#ifndef STACK_SIZE_VAR
   #define STACK_SIZE_VAR stack_size
#endif

struct explicit_stack_elem1 { void *ret_addr, *arg1; };
struct explicit_stack_elem2 { void *ret_addr, *arg1, *arg2; };

#define NOREC_LOOP_END() loop_end:
#define LOAD_ARG_FROM_STACK(n, t)                                      \
   ((t)(uptr)STACK_VAR[STACK_SIZE_VAR-1].arg##n)

#define CREATE_SHADOW_STACK(size, nargs)                               \
   struct explicit_stack_elem##nargs STACK_VAR[size];                  \
   int STACK_SIZE_VAR = 0;


#define SIMULATE_RETURN_NULL()                                         \
   {                                                                   \
      STACK_SIZE_VAR--;                                                \
      ASSERT(STACK_VAR[STACK_SIZE_VAR].ret_addr || !STACK_SIZE_VAR);   \
      continue;                                                        \
   }

#define HANDLE_SIMULATED_RETURN()                                      \
   {                                                                   \
      void *addr = STACK_VAR[STACK_SIZE_VAR].ret_addr;                 \
      if (addr != NULL)                                                \
         goto *addr;                                                   \
   }

#define SIMULATE_CALL1(a1)                                             \
   {                                                                   \
      VERIFY(STACK_SIZE_VAR < (int)ARRAY_SIZE(STACK_VAR));             \
      STACK_VAR[STACK_SIZE_VAR++] = (typeof(STACK_VAR[0])) {           \
         &&CONCAT(after_, __LINE__),                                   \
         TO_PTR(a1)                                                    \
      };                                                               \
      STACK_VAR[STACK_SIZE_VAR].ret_addr = NULL;                       \
      goto loop_end;                                                   \
      CONCAT(after_, __LINE__):;                                       \
   }

#define SIMULATE_CALL2(a1, a2)                                         \
   {                                                                   \
      VERIFY(STACK_SIZE_VAR < (int)ARRAY_SIZE(STACK_VAR));             \
      STACK_VAR[STACK_SIZE_VAR++] = (typeof(STACK_VAR[0])) {           \
         &&CONCAT(after_, __LINE__),                                   \
         TO_PTR(a1),                                                   \
         TO_PTR(a2)                                                    \
      };                                                               \
      STACK_VAR[STACK_SIZE_VAR].ret_addr = NULL;                       \
      goto loop_end;                                                   \
      CONCAT(after_, __LINE__):;                                       \
   }
