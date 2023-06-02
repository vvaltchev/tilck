/* SPDX-License-Identifier: BSD-2-Clause */
#pragma once

#define DECL_FUNCS_0(name, ret)                     \
   ret __real_##name();                             \
   ret __wrap_##name();

#define DECL_FUNCS_1(name, ret, t1)                 \
   ret __real_##name(t1);                           \
   ret __wrap_##name(t1);

#define DECL_FUNCS_2(name, ret, t1, t2)             \
   ret __real_##name(t1, t2);                       \
   ret __wrap_##name(t1, t2);

#define DECL_FUNCS_3(name, ret, t1, t2, t3)         \
   ret __real_##name(t1, t2, t3);                   \
   ret __wrap_##name(t1, t2, t3);

#define DECL_FUNCS_4(name, ret, t1, t2, t3, t4)     \
   ret __real_##name(t1, t2, t3, t4);               \
   ret __wrap_##name(t1, t2, t3, t4);



extern "C" {

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/datetime.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/sched.h>
#include <tilck/kernel/fs/vfs_base.h>

#define DEF_0(x, n, ret) DECL_FUNCS_0(n, ret)
#define DEF_1(x, n, ret, t1) DECL_FUNCS_1(n, ret, t1)
#define DEF_2(x, n, ret, t1, t2) DECL_FUNCS_2(n, ret, t1, t2)
#define DEF_3(x, n, ret, t1, t2, t3) DECL_FUNCS_3(n, ret, t1, t2, t3)
#define DEF_4(x, n, ret, t1, t2, t3, t4) DECL_FUNCS_4(n, ret, t1, t2, t3, t4)

#include "mocked_funcs.h"

#undef DEF_0
#undef DEF_1
#undef DEF_2
#undef DEF_3
#undef DEF_4
}

#define DEF_0(what, name, ret)                        \
   virtual ret name() {                               \
      return __##what##_##name();                     \
   }

#define DEF_1(what, name, ret, t1)                    \
   virtual ret name(t1 a1) {                          \
      return __##what##_##name(a1);                   \
   }

#define DEF_2(what, name, ret, t1, t2)                \
   virtual ret name(t1 a1, t2 a2) {                   \
      return __##what##_##name(a1, a2);               \
   }

#define DEF_3(what, name, ret, t1, t2, t3)            \
   virtual ret name(t1 a1, t2 a2, t3 a3) {            \
      return __##what##_##name(a1, a2, a3);           \
   }

#define DEF_4(what, name, ret, t1, t2, t3, t4)        \
   virtual ret name(t1 a1, t2 a2, t3 a3, t4 a4) {     \
      return __##what##_##name(a1, a2, a3, a4);       \
   }



class KernelSingleton {
public:

   KernelSingleton() {
      prevInstance = instance;
      instance = this;
   }

   virtual ~KernelSingleton() {
      instance = prevInstance;
   }

   #include "mocked_funcs.h"

   static KernelSingleton *get() { return instance; }

protected:
   static KernelSingleton *instance;
   KernelSingleton *prevInstance;
};

#undef DECL_FUNCS_0
#undef DECL_FUNCS_1
#undef DECL_FUNCS_2
#undef DECL_FUNCS_3
#undef DECL_FUNCS_4

#undef DEF_0
#undef DEF_1
#undef DEF_2
#undef DEF_3
#undef DEF_4
