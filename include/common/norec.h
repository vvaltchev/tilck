
#ifndef STACK_VAR
   #define STACK_VAR stack
#endif

#ifndef STACK_SIZE_VAR
   #define STACK_SIZE_VAR stack_size
#endif

typedef struct {

   void *ret_addr;
   void *arg1;

} stack_elem1;

typedef struct {

   void *ret_addr;
   void *arg1;
   void *arg2;

} stack_elem2;

#define CONCAT_(x,y) x##y
#define CONCAT(x,y) CONCAT_(x,y)

#define NOREC_LOOP_END() loop_end:

#define CREATE_SHADOW_STACK(size, nargs)           \
   stack_elem##nargs STACK_VAR[size];              \
   int STACK_SIZE_VAR = 0;

#define SIMULATE_CALL1(a1)                                             \
   {                                                                   \
      STACK_VAR[STACK_SIZE_VAR++] =                                    \
         (typeof(STACK_VAR[0])) {&&CONCAT(after_, __LINE__), (a1)};    \
      STACK_VAR[STACK_SIZE_VAR].ret_addr = NULL;                       \
      goto loop_end;                                                   \
      CONCAT(after_, __LINE__):;                                       \
   }

#define SIMULATE_CALL2(a1, a2)                                               \
   {                                                                         \
      STACK_VAR[STACK_SIZE_VAR++] =                                          \
         (typeof(STACK_VAR[0])) {&&CONCAT(after_, __LINE__), (a1), (a2)};    \
      STACK_VAR[STACK_SIZE_VAR].ret_addr = NULL;                             \
      goto loop_end;                                                         \
      CONCAT(after_, __LINE__):;                                             \
   }

#define LOAD_ARG_FROM_STACK(n, t) ((t)STACK_VAR[STACK_SIZE_VAR-1].arg##n)

#define SIMULATE_RETURN_NULL()                                         \
   {                                                                   \
      STACK_SIZE_VAR--;                                                \
      ASSERT(STACK_VAR[STACK_SIZE_VAR].ret_addr || !STACK_SIZE_VAR);   \
      continue;                                                        \
   }

#define HANDLE_SIMULATED_RETURN()                        \
   {                                                     \
      void *addr = STACK_VAR[STACK_SIZE_VAR].ret_addr;   \
      if (addr != NULL)                                  \
         goto *addr;                                     \
   }

