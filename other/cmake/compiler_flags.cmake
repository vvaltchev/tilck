# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

set(GENERAL_DEFS_LIST "")

if (CMAKE_BUILD_TYPE STREQUAL "Release")

   message(STATUS "Preparing a RELEASE build...")
   list(APPEND GENERAL_DEFS_LIST "-DNDEBUG -DTILCK_RELEASE_BUILD")

   if (TINY_KERNEL)
      set(OPT_FLAGS_LIST -Os)
   else()
      set(OPT_FLAGS_LIST -O3)
   endif()

else()

   message(STATUS "Preparing a DEBUG build...")
   list(APPEND GENERAL_DEFS_LIST "-DTILCK_DEBUG_BUILD")
   set(OPT_FLAGS_LIST -O0 -fno-inline-functions)

endif()

if (TEST_GCOV OR KERNEL_GCOV)
   if (NOT EXISTS ${TCROOT}/noarch/lcov)
      show_missing_lcov_error()
   endif()
endif()

if (TEST_GCOV)
   set(GCOV_COMPILE_FLAGS "-fprofile-arcs -ftest-coverage")
   set(GCOV_LINK_FLAGS "-fprofile-arcs -lgcov")
endif()

set(
   DBG_FLAGS_LIST

   -ggdb
   -fno-omit-frame-pointer
)

set(
   WARN_FLAGS_LIST

   -Wall
   -Wextra
   -Werror
   -Wshadow
   -Wvla
   -Wno-unused-function
   -Wno-unused-parameter
   -Wno-unused-label
)

if (CMAKE_C_COMPILER_ID STREQUAL "GNU")

   if (WCONV)
      show_wconv_warning()
   endif()

   if (CMAKE_C_COMPILER_VERSION VERSION_LESS "5.0.0")
      # See http://gcc.gnu.org/bugzilla/show_bug.cgi?id=53119
      list(APPEND WARN_FLAGS_LIST "-Wno-missing-braces")
      list(APPEND WARN_FLAGS_LIST "-Wno-missing-field-initializers")
   endif()
endif()

if (CMAKE_C_COMPILER_ID STREQUAL "Clang")

   list(APPEND WARN_FLAGS_LIST "-Wno-missing-braces")

   if (USE_SYSCC)
      show_clang_and_syscc_error()
   endif()

   if (NOT KERNEL_SYSCC)
      if (WCONV)
         show_wconv_warning()
      endif()
   endif()

endif()

set(
   FREESTANDING_FLAGS_LIST

   -ffreestanding
   -fno-builtin
   -mno-red-zone
)
JOIN("${FREESTANDING_FLAGS_LIST}" ${SPACE} FREESTANDING_FLAGS)

set(
   SAFER_BEHAVIOR_FLAGS_LIST

   -fwrapv
   -Wundef
)
JOIN("${SAFER_BEHAVIOR_FLAGS_LIST}" ${SPACE} SAFER_BEHAVIOR_FLAGS)

JOIN("${GENERAL_DEFS_LIST}"       ${SPACE} GENERAL_DEFS)
JOIN("${DBG_FLAGS_LIST}"          ${SPACE} DBG_FLAGS)
JOIN("${OPT_FLAGS_LIST}"          ${SPACE} OPT_FLAGS)
JOIN("${WARN_FLAGS_LIST}"         ${SPACE} WARN_FLAGS)
set(COMMON_FLAGS "${GENERAL_DEFS} ${DBG_FLAGS} ${OPT_FLAGS} ${WARN_FLAGS}")

# Kernel flags

set(GENERAL_KERNEL_FLAGS_LIST "")

list(
   APPEND GENERAL_KERNEL_FLAGS_LIST

   -D__TILCK_KERNEL__
   -fno-strict-aliasing

   ${FREESTANDING_FLAGS_LIST}
   ${SAFER_BEHAVIOR_FLAGS_LIST}
)

if (WCONV)
   list(APPEND GENERAL_KERNEL_FLAGS_LIST "-Wconversion")
endif()

JOIN("${GENERAL_KERNEL_FLAGS_LIST}" ${SPACE} GENERAL_KERNEL_FLAGS)
set(KERNEL_FLAGS "${COMMON_FLAGS} ${GENERAL_KERNEL_FLAGS}")

set(
   KERNEL_NO_ARCH_FLAGS_LIST

   -DKERNEL_TEST
   -fPIC
   ${KERNEL_FLAGS}
   ${GCOV_COMPILE_FLAGS}
)

JOIN("${KERNEL_NO_ARCH_FLAGS_LIST}" ${SPACE} KERNEL_NO_ARCH_FLAGS)

set(
   KERNEL_CXX_FLAGS_LIST

   -fno-exceptions
   -fno-use-cxa-atexit
   -fno-rtti
)

JOIN("${KERNEL_CXX_FLAGS_LIST}" ${SPACE} KERNEL_CXX_FLAGS)

set(
   LOWLEVEL_BINARIES_FLAGS_LIST

   -fno-pic
   -fno-exceptions
   -fno-stack-protector
   -fno-asynchronous-unwind-tables

   # Allow easier disassembly debugging
   # -mpush-args
   # -mno-accumulate-outgoing-args
   # -mno-stack-arg-probe
)
JOIN("${LOWLEVEL_BINARIES_FLAGS_LIST}" ${SPACE} LOWLEVEL_BINARIES_FLAGS)


set(
   DISABLE_FPU_FLAGS_LIST

   # Disable the generation of any kind of FPU instructions
   -mno-80387
   -mno-mmx
   -mno-sse
   -mno-avx
)
JOIN("${DISABLE_FPU_FLAGS_LIST}" ${SPACE} DISABLE_FPU_FLAGS)
