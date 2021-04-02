# SPDX-License-Identifier: BSD-2-Clause
cmake_minimum_required(VERSION 3.2)

set(GENERAL_DEFS_LIST "")

if (CMAKE_BUILD_TYPE STREQUAL "Release")

   message(STATUS "Preparing a RELEASE build...")
   list(APPEND GENERAL_DEFS_LIST "-DNDEBUG -DTILCK_RELEASE_BUILD")
   set(OPT_FLAGS_LIST -O3)

elseif (CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")

   message(STATUS "Preparing a MinSizeRel build...")
   list(APPEND GENERAL_DEFS_LIST "-DNDEBUG -DTILCK_RELEASE_BUILD")
   set(OPT_FLAGS_LIST -Os)

elseif (CMAKE_BUILD_TYPE STREQUAL "Debug")

   message(STATUS "Preparing a DEBUG build...")
   list(APPEND GENERAL_DEFS_LIST "-DTILCK_DEBUG_BUILD")
   set(OPT_FLAGS_LIST -O0 -fno-inline-functions)

else()

   message(FATAL_ERROR "Unknown build type: '${CMAKE_BUILD_TYPE}'")

endif()

if (TEST_GCOV OR KERNEL_GCOV)
   if (NOT EXISTS ${TCROOT}/noarch/lcov-${LCOV_VER})
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
   -Wcast-align
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

   -fno-strict-aliasing
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

   ${FREESTANDING_FLAGS_LIST}
   ${SAFER_BEHAVIOR_FLAGS_LIST}
)

if (WCONV)
   list(APPEND GENERAL_KERNEL_FLAGS_LIST "-Wconversion")
endif()

if (KERNEL_UBSAN)

   list(
      APPEND GENERAL_KERNEL_FLAGS_LIST

      -fsanitize=shift,shift-exponent,shift-base,integer-divide-by-zero
      -fsanitize=nonnull-attribute,returns-nonnull-attribute
      -fsanitize=bool,enum
      -fsanitize=unreachable
      -fsanitize=null
   )

   #
   # Cannot enable -fsanitize=alignment, with Clang because it generates leaf
   # functions which misalign the stack pointer. What happens when get an IRQ
   # while running one of those functions? If UBSAN is enabled, we'll get
   # panic, otherwise everything is fine, because x86 allows unaligned access.
   #
   # See:
   #  - https://bugs.llvm.org/show_bug.cgi?id=49828
   #  - https://forum.osdev.org/viewtopic.php?f=13&t=42430&start=0
   #

   if (CMAKE_C_COMPILER_ID STREQUAL "GNU")

      list(
         APPEND GENERAL_KERNEL_FLAGS_LIST

         -fsanitize=alignment
         -fsanitize-recover=alignment
      )

   endif()
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
