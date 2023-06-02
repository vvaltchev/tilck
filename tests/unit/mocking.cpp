/* SPDX-License-Identifier: BSD-2-Clause */

#define DEF_FINST_0(name, ret)                 \
   ret name() {                                \
      return KernelSingleton::get()->name();   \
   }

#define DEF_FINST_1(name, ret, t1)             \
   ret name(t1 a1) {                           \
      return KernelSingleton::get()->name(a1); \
   }

#define DEF_FINST_2(name, ret, t1, t2)             \
   ret name(t1 a1, t2 a2) {                        \
      return KernelSingleton::get()->name(a1, a2); \
   }

#define DEF_FINST_3(name, ret, t1, t2, t3)              \
   ret name(t1 a1, t2 a2, t3 a3) {                      \
      return KernelSingleton::get()->name(a1, a2, a3);  \
   }

#define DEF_FINST_4(name, ret, t1, t2, t3, t4)              \
   ret name(t1 a1, t2 a2, t3 a3, t4 a4) {                   \
      return KernelSingleton::get()->name(a1, a2, a3, a4);  \
   }




#include "mocking.h"
KernelSingleton *KernelSingleton::instance = new KernelSingleton();

extern "C" {

#define DEF_0(x, n, ret) DEF_FINST_0(n, ret)
#define DEF_1(x, n, ret, t1) DEF_FINST_1(n, ret, t1)
#define DEF_2(x, n, ret, t1, t2) DEF_FINST_2(n, ret, t1, t2)
#define DEF_3(x, n, ret, t1, t2, t3) DEF_FINST_3(n, ret, t1, t2, t3)
#define DEF_4(x, n, ret, t1, t2, t3, t4) DEF_FINST_4(n, ret, t1, t2, t3, t4)

#include "mocked_funcs.h"
}
